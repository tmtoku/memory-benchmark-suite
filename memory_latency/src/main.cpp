#include "common.hpp"
#include "perf_counter.h"
#include "perf_events.hpp"
#include "utils.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

namespace memory_latency
{
    namespace
    {
        enum class BenchmarkTarget : std::uint8_t
        {
            Cache,
            TLB
        };

        struct BenchmarkResult
        {
            std::uint64_t cycle_count = std::numeric_limits<uint64_t>::max();
            std::uint64_t l1d_miss_count = 0;
            std::uint64_t l2_miss_count = 0;
            std::uint64_t l3_miss_count = 0;
            std::uint64_t l1_tlb_miss_count = 0;
            std::uint64_t l2_tlb_miss_count = 0;
        };

        void print_csv_header()
        {
            std::cout << "BufferSize,PaddedElementSize,PageSize,NumLogicalLoads,Cycles,L1DMisses,L2Misses,L3Misses,"
                         "L1TLBMisses,L2TLBMisses\n";
        }

        template <BenchmarkTarget BENCHMARK_TARGET>
        void print_csv_row(const std::size_t buffer_size, const std::size_t padded_element_size,
                           const std::size_t page_size, const std::int32_t num_logical_loads,
                           const BenchmarkResult& result)
        {
            std::cout << buffer_size << "," << padded_element_size << "," << page_size << "," << num_logical_loads
                      << "," << result.cycle_count << ",";
            if constexpr (BENCHMARK_TARGET == BenchmarkTarget::Cache)
            {
                std::cout << result.l1d_miss_count << "," << result.l2_miss_count << "," << result.l3_miss_count
                          << ",,\n";
            }
            else
            {
                std::cout << ",,," << result.l1_tlb_miss_count << "," << result.l2_tlb_miss_count << "\n";
            }
        }

        template <BenchmarkTarget BENCHMARK_TARGET>
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

            if constexpr (BENCHMARK_TARGET == BenchmarkTarget::Cache)
            {
                auto l1d_miss_counter = open_counter(perf_events::L1D_MISS, group_fd);
                auto l2_miss_counter = open_counter(perf_events::L2_MISS, group_fd);
                auto l3_miss_counter = open_counter(perf_events::L3_MISS, group_fd);

                if (!perf_counter_is_valid(&l1d_miss_counter) || !perf_counter_is_valid(&l2_miss_counter) ||
                    !perf_counter_is_valid(&l3_miss_counter))
                {
                    close_counter(&l1d_miss_counter);
                    close_counter(&l2_miss_counter);
                    close_counter(&l3_miss_counter);
                    return std::nullopt;
                }

                perf_counter_enable(&cycle_counter);

                for (std::int32_t i = 0; i < NUM_WARMUPS + NUM_TRIALS; ++i)
                {
                    const auto start_l1d = perf_counter_read(&l1d_miss_counter);
                    const auto start_l2 = perf_counter_read(&l2_miss_counter);
                    const auto start_l3 = perf_counter_read(&l3_miss_counter);
                    const auto start_cycles = perf_counter_read(&cycle_counter);

                    const auto* const last_ptr = walk_pointer_chain<NUM_LOGICAL_LOADS>(start_ptr);
                    __asm__ volatile("" ::"r"(last_ptr));

                    const auto end_cycles = perf_counter_read(&cycle_counter);
                    const auto end_l3 = perf_counter_read(&l3_miss_counter);
                    const auto end_l2 = perf_counter_read(&l2_miss_counter);
                    const auto end_l1d = perf_counter_read(&l1d_miss_counter);

                    const auto cycles = end_cycles - start_cycles;
                    if (i >= NUM_WARMUPS && cycles < result.cycle_count)
                    {
                        result.cycle_count = cycles;
                        result.l1d_miss_count = end_l1d - start_l1d;
                        result.l2_miss_count = end_l2 - start_l2;
                        result.l3_miss_count = end_l3 - start_l3;
                    }
                }

                perf_counter_disable(&cycle_counter);
                close_counter(&l3_miss_counter);
                close_counter(&l2_miss_counter);
                close_counter(&l1d_miss_counter);
            }
            else
            {
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
            }

            return result;
        }

        template <BenchmarkTarget BENCHMARK_TARGET, bool USE_HUGEPAGE>
        void run_benchmark(const std::size_t buffer_size_in_bytes, const std::size_t padded_bytes_per_element)
        {
            constexpr auto NUM_LOGICAL_LOADS = std::int32_t{1'000'000};
            constexpr auto NUM_SEEDS = std::size_t{5};

            if (buffer_size_in_bytes % padded_bytes_per_element != 0)
            {
                std::cerr << "Error: `buffer_size_in_bytes` must be a multiple of `padded_bytes_per_element`\n";
                return;
            }
            const auto num_elements = buffer_size_in_bytes / padded_bytes_per_element;

            auto buffer = [buffer_size_in_bytes] {
                if constexpr (USE_HUGEPAGE)
                {
                    return common::allocate_hugepage_buffer<MemoryAddress>(buffer_size_in_bytes);
                }
                else
                {
                    return common::allocate_aligned_buffer<MemoryAddress>(buffer_size_in_bytes,
                                                                          common::get_page_size());
                }
            }();

            auto cycle_counter = perf_counter_open_by_name(perf_events::CYCLES, -1);
            if (!perf_counter_is_valid(&cycle_counter))
            {
                std::cerr << "Error: Failed to open performance counter for event '" << perf_events::CYCLES << "'.\n";
                return;
            }

            std::array<BenchmarkResult, NUM_SEEDS> seed_results;
            for (std::size_t i = 0; i < NUM_SEEDS; ++i)
            {
                auto* const start_ptr =
                    generate_random_pointer_chasing(buffer.get(), num_elements, padded_bytes_per_element, i);
                const auto result = measure_pointer_chasing<BENCHMARK_TARGET>(start_ptr, cycle_counter);
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

            const auto page_size = USE_HUGEPAGE ? common::get_hugepage_size() : common::get_page_size();
            print_csv_row<BENCHMARK_TARGET>(buffer_size_in_bytes, padded_bytes_per_element, page_size,
                                            NUM_LOGICAL_LOADS, median_result);
        }
    }  // namespace
}  // namespace memory_latency

int main(int argc, char* argv[])
{
    using namespace memory_latency;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <cache|tlb>\n", argv[0]);
        return 1;
    }
    const auto benchmark_target = std::string_view(argv[1]);

    try
    {
        const auto cache_line_bytes = common::get_cache_line_bytes();

        constexpr auto MAX_BUFFER_SIZE = 128 * common::MiB;
        constexpr auto NUM_BINS = std::size_t{4};

        if (benchmark_target == "cache")
        {
            constexpr auto MIN_SIZE = 16 * common::KiB;
            constexpr auto MIN_STEP = MIN_SIZE / NUM_BINS;

            print_csv_header();

            for (auto start_size = MIN_SIZE, step = MIN_STEP; start_size <= MAX_BUFFER_SIZE; start_size *= 2, step *= 2)
            {
                for (auto size = start_size; size <= MAX_BUFFER_SIZE && size < start_size * 2; size += step)
                {
                    run_benchmark<BenchmarkTarget::Cache, true>(size, cache_line_bytes);
                }
            }
        }
        else if (benchmark_target == "tlb")
        {
            const auto min_step = common::get_page_size() + cache_line_bytes;
            const auto min_size = NUM_BINS * min_step;

            print_csv_header();

            for (auto start_size = min_size, step = min_step; start_size <= MAX_BUFFER_SIZE; start_size *= 2, step *= 2)
            {
                for (auto size = start_size; size <= MAX_BUFFER_SIZE && size < start_size * 2; size += step)
                {
                    run_benchmark<BenchmarkTarget::TLB, true>(size, min_step);
                }
            }

            for (auto start_size = min_size, step = min_step; start_size <= MAX_BUFFER_SIZE; start_size *= 2, step *= 2)
            {
                for (auto size = start_size; size <= MAX_BUFFER_SIZE && size < start_size * 2; size += step)
                {
                    run_benchmark<BenchmarkTarget::TLB, false>(size, min_step);
                }
            }
        }
        else
        {
            fprintf(stderr, "Unknown benchmark target: %s\n", argv[1]);
            return 1;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
