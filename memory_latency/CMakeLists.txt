add_executable(cache_latency
    src/cache_latency.cpp
    src/utils.hpp
)

target_link_libraries(cache_latency PRIVATE
    micro_benchmark_common
    perf_counter::perf_counter
)
