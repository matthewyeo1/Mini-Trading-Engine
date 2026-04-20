# Order class
add_executable(bench_order bench_order.cpp)
add_executable(bench_price_level bench_price_level.cpp)
add_executable(bench_order_book bench_order_book.cpp)

# Link libraries for each
foreach(bench_target bench_order bench_price_level bench_order_book)
    target_link_libraries(${bench_target} 
        velox_core 
        benchmark::benchmark
    )
    target_include_directories(${bench_target} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/whirlpool/include
    )
    if(WIN32)
        target_link_libraries(${bench_target} ws2_32 shlwapi)
    endif()
    set_target_properties(${bench_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/benchmarks/Release"
    )
endforeach()

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