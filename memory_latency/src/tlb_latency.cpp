#include "common.hpp"
#include "perf_counter.h"
#include "perf_events.hpp"
#include "utils.hpp"

#include <linux/memfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>

namespace memory_latency
{
    namespace
    {
        struct BenchmarkResult
        {
            std::uint64_t cycle_count = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t l1_tlb_miss_count = 0;
            std::uint64_t l2_tlb_miss_count = 0;
        };

        void print_csv_header() { std::cout << "BufferSize,PageSize,NumLogicalLoads,Cycles,L1TLBMisses,L2TLBMisses\n"; }

        void print_csv_row(const std::size_t buffer_size, const std::size_t page_size,
                           const std::int32_t num_logical_loads, const BenchmarkResult& result)
        {
            std::cout << buffer_size << "," << page_size << "," << num_logical_loads << "," << result.cycle_count << ","
                      << result.l1_tlb_miss_count << "," << result.l2_tlb_miss_count << "\n";
        }

        [[nodiscard]] std::int32_t allocate_backing_pages(const std::size_t num_elements)
        {
            const auto page_size = common::get_page_size();
            const auto num_elements_per_page = page_size / sizeof(MemoryAddress);
            const auto num_pages = (num_elements + num_elements_per_page - 1) / num_elements_per_page;
            const auto total_bytes = num_pages * page_size;

            const auto fd = static_cast<std::int32_t>(memfd_create("backing_pages", MFD_CLOEXEC));
            if (fd == -1)
            {
                throw std::runtime_error("Failed to create the backing pages.");
            }

            if (ftruncate(fd, static_cast<off_t>(total_bytes)) == -1)
            {
                close(fd);
                throw std::runtime_error("Failed to extend the backing pages.");
            }

            return fd;
        }

        [[nodiscard]] std::unique_ptr<MemoryAddress, common::MunmapDeleter> allocate_virtual_pages(
            const std::size_t num_virtual_pages)
        {
            const auto total_bytes = num_virtual_pages * common::get_page_size();
            auto* const virtual_pages = mmap(nullptr, total_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (virtual_pages == MAP_FAILED)
            {
                throw std::bad_alloc();
            }

            return {reinterpret_cast<MemoryAddress*>(virtual_pages), common::MunmapDeleter{total_bytes}};
        }

        void map_virtual_pages(const std::int32_t backing_fd, MemoryAddress* const virtual_pages,
                               const std::size_t num_virtual_pages)
        {
            const auto page_size = common::get_page_size();
            const auto num_slots_per_page = page_size / sizeof(MemoryAddress);

            for (std::size_t virtual_page_index = 0; virtual_page_index < num_virtual_pages; ++virtual_page_index)
            {
                const auto virtual_page_offset = virtual_page_index * page_size;
                auto* const virtual_page_addr = reinterpret_cast<unsigned char*>(virtual_pages) + virtual_page_offset;

                const auto backing_page_index = virtual_page_index / num_slots_per_page;
                const auto backing_page_offset = static_cast<off_t>(backing_page_index * page_size);

                auto* const mapped_page = mmap(virtual_page_addr, page_size, PROT_READ | PROT_WRITE,
                                               MAP_SHARED | MAP_FIXED, backing_fd, backing_page_offset);
                if (mapped_page == MAP_FAILED)
                {
                    throw std::bad_alloc();
                }
            }
        }

        [[nodiscard]] MemoryAddress* generate_random_pointer_chasing(MemoryAddress* const virtual_pages,
                                                                     const std::size_t num_virtual_pages,
                                                                     const std::uint64_t seed)
        {
            const auto get_element_location = [virtual_pages, page_size = common::get_page_size()](
                                                  const std::size_t virtual_page_index) {
                const auto virtual_page_offset = virtual_page_index * page_size;
                auto* const virtual_page_addr = reinterpret_cast<unsigned char*>(virtual_pages) + virtual_page_offset;

                const auto num_slots_per_page = page_size / sizeof(MemoryAddress);
                const auto backing_slot_index = virtual_page_index % num_slots_per_page;
                const auto backing_slot_offset = backing_slot_index * sizeof(MemoryAddress);

                return reinterpret_cast<MemoryAddress*>(virtual_page_addr + backing_slot_offset);
            };

            return detail::generate_random_pointer_chasing(num_virtual_pages, get_element_location, seed);
        }

        std::optional<BenchmarkResult> measure_pointer_chasing(MemoryAddress* const start_ptr,
                                                               perf_counter& cycle_counter)
        {
            constexpr auto NUM_LOGICAL_LOADS = std::int32_t{1'000'000};
            constexpr auto NUM_TRIALS = std::int32_t{10};
            constexpr auto NUM_WARMUPS = std::int32_t{3};

            const auto open_counter = [](const char* name, const std::int32_t group_fd) {
                const auto counter = perf_counter_open_by_name(name, group_fd);
                if (!perf_counter_is_valid(&counter))
                {
                    std::cerr << "Error: Failed to open performance counter for event '" << name << "'.\n";
                }
                return counter;
            };

            const auto close_counter = [](perf_counter* const counter) {
                if (perf_counter_is_valid(counter))
                {
                    perf_counter_close(counter);
                }
            };

            const auto group_fd = cycle_counter.fd;

            BenchmarkResult result;

            auto l1_tlb_miss_counter = open_counter(perf_events::L1_TLB_MISS, group_fd);
            auto l2_tlb_miss_counter = open_counter(perf_events::L2_TLB_MISS, group_fd);

            if (!perf_counter_is_valid(&l1_tlb_miss_counter) || !perf_counter_is_valid(&l2_tlb_miss_counter))
            {
                close_counter(&l1_tlb_miss_counter);
                close_counter(&l2_tlb_miss_counter);
                return std::nullopt;
            }

            perf_counter_enable(&cycle_counter);

            for (std::int32_t i = 0; i < NUM_WARMUPS + NUM_TRIALS; ++i)
            {
                const auto start_l1_tlb = perf_counter_read(&l1_tlb_miss_counter);
                const auto start_l2_tlb = perf_counter_read(&l2_tlb_miss_counter);
                const auto start_cycles = perf_counter_read(&cycle_counter);

                const auto* const last_ptr = walk_pointer_chain<NUM_LOGICAL_LOADS>(start_ptr);
                __asm__ volatile("" ::"r"(last_ptr));

                const auto end_cycles = perf_counter_read(&cycle_counter);
                const auto end_l2_tlb = perf_counter_read(&l2_tlb_miss_counter);
                const auto end_l1_tlb = perf_counter_read(&l1_tlb_miss_counter);

                const auto cycles = end_cycles - start_cycles;
                if (i >= NUM_WARMUPS && cycles < result.cycle_count)
                {
                    result.cycle_count = cycles;
                    result.l1_tlb_miss_count = end_l1_tlb - start_l1_tlb;
                    result.l2_tlb_miss_count = end_l2_tlb - start_l2_tlb;
                }
            }

            perf_counter_disable(&cycle_counter);
            close_counter(&l2_tlb_miss_counter);
            close_counter(&l1_tlb_miss_counter);

            return result;
        }

        void run_benchmark(const std::size_t buffer_size_in_bytes)
        {
            constexpr auto NUM_LOGICAL_LOADS = std::int32_t{1'000'000};
            constexpr auto NUM_SEEDS = std::size_t{5};

            const auto page_size = common::get_page_size();
            if (buffer_size_in_bytes % page_size != 0)
            {
                throw std::invalid_argument("`buffer_size_in_bytes` must be a multiple of the page size.");
            }

            const auto num_virtual_pages = buffer_size_in_bytes / page_size;
            const auto backing_fd = allocate_backing_pages(num_virtual_pages);
            auto virtual_pages = allocate_virtual_pages(num_virtual_pages);
            map_virtual_pages(backing_fd, virtual_pages.get(), num_virtual_pages);
            close(backing_fd);

            auto cycle_counter = perf_counter_open_by_name(perf_events::CYCLES, -1);
            if (!perf_counter_is_valid(&cycle_counter))
            {
                std::cerr << "Error: Failed to open performance counter for event '" << perf_events::CYCLES << "'.\n";
                return;
            }

            std::array<BenchmarkResult, NUM_SEEDS> seed_results;
            for (std::size_t i = 0; i < NUM_SEEDS; ++i)
            {
                auto* const start_ptr = generate_random_pointer_chasing(virtual_pages.get(), num_virtual_pages, i);
                const auto result = measure_pointer_chasing(start_ptr, cycle_counter);
                if (!result)
                {
                    perf_counter_close(&cycle_counter);
                    return;
                }
                seed_results[i] = result.value();
            }

            perf_counter_close(&cycle_counter);

            std::nth_element(seed_results.begin(), seed_results.begin() + (NUM_SEEDS / 2), seed_results.end(),
                             [](const auto& a, const auto& b) { return a.cycle_count < b.cycle_count; });
            const auto& median_result = seed_results[NUM_SEEDS / 2];
            print_csv_row(buffer_size_in_bytes, page_size, NUM_LOGICAL_LOADS, median_result);
        }
    }  // namespace
}  // namespace memory_latency

int main()
{
    using namespace memory_latency;

    try
    {
        constexpr auto MIN_SIZE = 128 * common::KiB;
        constexpr auto MAX_SIZE = 16 * common::MiB;
        constexpr auto NUM_BINS = std::size_t{4};
        constexpr auto MIN_STEP = MIN_SIZE / NUM_BINS;

        print_csv_header();

        for (auto start_size = MIN_SIZE, step = MIN_STEP; start_size <= MAX_SIZE; start_size *= 2, step *= 2)
        {
            for (auto size = start_size; size <= MAX_SIZE && size < start_size * 2; size += step)
            {
                run_benchmark(size);
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
