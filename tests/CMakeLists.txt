add_executable(velox_tests
    test_main.cpp
    test_order.cpp
    test_price_level.cpp
    #test_spsc_queue.cpp
    #test_ring_buffer.cpp
    #test_mpmc_queue.cpp
    #test_treiber_stack.cpp
    #test_object_pool.cpp
    #test_hashmap.cpp
    #test_order_book.cpp
    #test_matching_engine.cpp
)

target_link_libraries(velox_tests 
    velox_core 
    whirlpool
    gtest
    gtest_main      
)

target_include_directories(velox_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/whirlpool/include
    ${CMAKE_SOURCE_DIR}/third_party/googletest/googletest/include  
)

if(CI_BUILD)
    target_compile_definitions(velox_tests PRIVATE CI_BUILD=1)
endif()

add_test(NAME velox_tests COMMAND velox_tests)