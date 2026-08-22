#include "perf_counter.h"
#include "perf_events.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace integer_division
{
    namespace
    {
        template <std::int32_t NUM_ITERATIONS>
        [[nodiscard]] std::uint64_t run_auxiliary_dependency_chain()
        {
            auto rax_value = std::uint64_t{1};
            for (std::int32_t i = 0; i < NUM_ITERATIONS; ++i)
            {
                __asm__ volatile(
                    ".intel_syntax noprefix\n\t"
                    "and rax, rax\n\t"
                    "and rax, rax\n\t"
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
                    "or rax, rax\n\t"
                    "or rax, rax\n\t"
                    ".att_syntax prefix\n\t"
                    : "+a"(rax_value)
                    :
                    : "cc");
            }
            return rax_value;
        }

        [[nodiscard]] double measure_latency()
        {
            constexpr auto NUM_WARMUPS = std::int32_t{3};
            constexpr auto NUM_TRIALS = std::int32_t{10};
            constexpr auto NUM_ITERATIONS = std::int32_t{100'000};

            auto cycle_counter = perf_counter_open_by_name(perf_events::CYCLES, -1);
            if (!perf_counter_is_valid(&cycle_counter))
            {
                std::cerr << "Error: Failed to open performance counter for event '" << perf_events::CYCLES << "'.\n";
                return 1;
            }

            auto cycle_count = std::numeric_limits<std::uint64_t>::max();
            perf_counter_enable(&cycle_counter);

            for (std::int32_t i = 0; i < NUM_WARMUPS + NUM_TRIALS; ++i)
            {
                const auto start_cycles = perf_counter_read(&cycle_counter);

                const auto result = run_auxiliary_dependency_chain<NUM_ITERATIONS>();
                __asm__ volatile("" : : "r"(result));

                const auto end_cycles = perf_counter_read(&cycle_counter);

                const auto elapsed_cycles = end_cycles - start_cycles;
                if (i >= NUM_WARMUPS && elapsed_cycles < cycle_count)
                {
                    cycle_count = elapsed_cycles;
                }
            }

            perf_counter_disable(&cycle_counter);
            perf_counter_close(&cycle_counter);

            return static_cast<double>(cycle_count) / static_cast<double>(NUM_ITERATIONS);
        }
    }  // namespace
}  // namespace integer_division

int main()
{
    std::cout << "Auxiliary dependency chain latency: " << std::fixed << std::setprecision(2)
              << integer_division::measure_latency() << " cycles\n";
    return 0;
}
