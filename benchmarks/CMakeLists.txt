add_executable(bench_order bench_order.cpp)

target_link_libraries(bench_order 
    velox_core 
    whirlpool     
    benchmark::benchmark
)

target_include_directories(bench_order PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/whirlpool/include
)

# Windows specific
if(WIN32)
    target_link_libraries(bench_order 
        ws2_32
        shlwapi
    )
endif()

# Set output directory
set_target_properties(bench_order PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/benchmarks/Release"
)