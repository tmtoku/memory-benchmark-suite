#include "perf_counter.h"
#include "perf_events.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>

namespace integer_division
{
    namespace
    {
        struct BenchmarkResult
        {
            std::uint64_t cycle_count = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t divider_busy_cycle_count = 0;
        };

        void print_csv_header()
        {
            std::cout << "Benchmark,NumBits,Dividend,Divisor,NumIterations,LatencyCycles,DividerBusyCycles\n";
        }

        void print_csv_row(const char* const benchmark_name, const std::int32_t num_bits, const std::uint64_t dividend,
                           const std::uint64_t divisor, const std::int32_t num_iterations,
                           const BenchmarkResult& result)
        {
            std::cout << benchmark_name << ',' << num_bits << ',' << dividend << ',' << divisor << ',' << num_iterations
                      << ',' << result.cycle_count << ',' << result.divider_busy_cycle_count << '\n';
        }

        template <std::int32_t NUM_ITERATIONS>
        [[nodiscard]] std::uint64_t run_idiv_kernel(const std::uint64_t dividend, const std::uint64_t divisor)
        {
            auto restored_dividend = dividend;
            for (std::int32_t i = 0; i < NUM_ITERATIONS; ++i)
            {
                // Add a 16-cycle auxiliary dependency chain to not exceed the divider's throughput limit.
                // Its known latency is subtracted from the measured cycles.
                __asm__ volatile(
                    ".intel_syntax noprefix\n\t"
                    "xor edx, edx\n\t"           // RDX = 0
                    "idiv %V[divisor]\n\t"       // Divide RDX:RAX; quotient -> RAX, remainder -> RDX
                    "and rax, rdx\n\t"           // Wait for both quotient and remainder
                    "and rax, %V[dividend]\n\t"  // RAX = (RAX & dividend) | dividend = dividend
                    "or rax, %V[dividend]\n\t"   //
                    "or rax, rax\n\t"            // Add 13 cycles to the dependency chain.
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    ".att_syntax prefix\n\t"
                    : "+&a"(restored_dividend)
                    : [dividend] "r"(dividend), [divisor] "r"(divisor)
                    : "rdx", "cc");
            }

            return restored_dividend;
        }

        template <std::int32_t NUM_ITERATIONS, typename LatencyKernel>
        [[nodiscard]] std::optional<BenchmarkResult> measure_latency(perf_counter& cycle_counter,
                                                                     const LatencyKernel& run_latency_kernel)
        {
            constexpr auto NUM_WARMUPS = std::int32_t{3};
            constexpr auto NUM_TRIALS = std::int32_t{10};
            constexpr auto AUXILIARY_CHAIN_CYCLES = std::uint64_t{16};
            constexpr auto TOTAL_AUXILIARY_CHAIN_CYCLES =
                AUXILIARY_CHAIN_CYCLES * static_cast<std::uint64_t>(NUM_ITERATIONS);

            const auto open_counter = [](const char* const name, const std::int32_t group_fd) {
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
            auto divider_busy_cycle_counter = open_counter(perf_events::DIVIDER_BUSY_CYCLES, group_fd);
            if (!perf_counter_is_valid(&divider_busy_cycle_counter))
            {
                return std::nullopt;
            }

            BenchmarkResult result;

            perf_counter_enable(&cycle_counter);

            for (std::int32_t i = 0; i < NUM_WARMUPS + NUM_TRIALS; ++i)
            {
                const auto start_divider_busy_cycles = perf_counter_read(&divider_busy_cycle_counter);
                const auto start_cycles = perf_counter_read(&cycle_counter);

                const auto kernel_result = run_latency_kernel();
                __asm__ volatile("" : : "r"(kernel_result));

                const auto end_cycles = perf_counter_read(&cycle_counter);
                const auto end_divider_busy_cycles = perf_counter_read(&divider_busy_cycle_counter);

                const auto target_cycles = (end_cycles - start_cycles) - TOTAL_AUXILIARY_CHAIN_CYCLES;
                if (i >= NUM_WARMUPS && target_cycles < result.cycle_count)
                {
                    result.cycle_count = target_cycles;
                    result.divider_busy_cycle_count = end_divider_busy_cycles - start_divider_busy_cycles;
                }
            }

            perf_counter_disable(&cycle_counter);
            close_counter(&divider_busy_cycle_counter);

            return result;
        }

        void run_benchmark()
        {
            constexpr auto NUM_ITERATIONS = std::int32_t{100'000};
            constexpr auto MIN_BITS = std::int32_t{1};
            constexpr auto MAX_BITS = std::numeric_limits<std::int64_t>::digits;

            auto cycle_counter = perf_counter_open_by_name(perf_events::CYCLES, -1);
            if (!perf_counter_is_valid(&cycle_counter))
            {
                std::cerr << "Error: Failed to open performance counter for event '" << perf_events::CYCLES << "'.\n";
                return;
            }

            // Benchmark 1: Various dividend bits
            constexpr auto DIVISOR = std::uint64_t{1};
            for (std::int32_t dividend_bits = MIN_BITS; dividend_bits <= MAX_BITS; ++dividend_bits)
            {
                const auto dividend = std::uint64_t{1} << (dividend_bits - 1);

                const auto result = measure_latency<NUM_ITERATIONS>(
                    cycle_counter, [dividend] { return run_idiv_kernel<NUM_ITERATIONS>(dividend, DIVISOR); });
                if (!result)
                {
                    perf_counter_close(&cycle_counter);
                    return;
                }
                print_csv_row("dividend", dividend_bits, dividend, DIVISOR, NUM_ITERATIONS, result.value());
            }

            // Benchmark 2: Various quotient bits
            constexpr auto DIVIDEND = std::uint64_t{1} << (MAX_BITS - 1);
            for (std::int32_t quotient_bits = MIN_BITS; quotient_bits <= MAX_BITS; ++quotient_bits)
            {
                const auto divisor = std::uint64_t{1} << (MAX_BITS - quotient_bits);

                const auto result = measure_latency<NUM_ITERATIONS>(
                    cycle_counter, [divisor] { return run_idiv_kernel<NUM_ITERATIONS>(DIVIDEND, divisor); });
                if (!result)
                {
                    perf_counter_close(&cycle_counter);
                    return;
                }
                print_csv_row("quotient", quotient_bits, DIVIDEND, divisor, NUM_ITERATIONS, result.value());
            }

            perf_counter_close(&cycle_counter);
        }
    }  // namespace
}  // namespace integer_division

int main()
{
    try
    {
        integer_division::print_csv_header();
        integer_division::run_benchmark();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
