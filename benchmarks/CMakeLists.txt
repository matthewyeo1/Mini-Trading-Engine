find_package(benchmark REQUIRED)

add_executable(velox_benchmarks
    bench_primitives.cpp
    bench_pipeline.cpp
    bench_book.cpp
)

target_link_libraries(velox_benchmarks 
    velox_core 
    whirlpool     
    benchmark::benchmark 
    pthread
)

target_include_directories(velox_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/whirlpool/include 
)