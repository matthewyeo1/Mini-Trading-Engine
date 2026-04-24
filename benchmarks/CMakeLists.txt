# Order class
add_executable(bench_order bench_order.cpp)
add_executable(bench_price_level bench_price_level.cpp)
add_executable(bench_order_book bench_order_book.cpp)
add_executable(bench_matching_engine bench_matching_engine.cpp)
add_executable(bench_risk_manager bench_risk_manager.cpp)
add_executable(bench_execution_gateway bench_execution_gateway.cpp)
add_executable(bench_feed_handler bench_feed_handler.cpp)
add_executable(bench_book_snapshot bench_book_snapshot.cpp)
add_executable(bench_position_manager bench_position_manager.cpp)

# Link libraries for each
foreach(bench_target 
        bench_order 
        bench_price_level 
        bench_order_book
        bench_matching_engine
        bench_risk_manager
        bench_execution_gateway
        bench_feed_handler
        bench_book_snapshot
        bench_position_manager)
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