add_executable(integer_division
    src/main.cpp
)

target_link_libraries(integer_division PRIVATE
    micro_benchmark_common
    perf_counter::perf_counter
)

add_executable(auxiliary_dependency_chain
    src/auxiliary_dependency_chain.cpp
)

target_link_libraries(auxiliary_dependency_chain PRIVATE
    micro_benchmark_common
    perf_counter::perf_counter
)
