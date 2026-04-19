add_library(velox_core STATIC
    # Feed handlers
    feed/feed_handler.cpp
    feed/decoder.cpp
    
    # Order book
    book/order_book.cpp
    book/price_level.cpp
    book/book_snapshot.cpp
    
    # Matching
    matching/matching_engine.cpp
    matching/order.cpp
    
    # Risk
    risk/risk_manager.cpp
    risk/position_manager.cpp
    
    # Gateway
    gateway/execution_gateway.cpp
    gateway/fix_encoder.cpp
)

target_link_libraries(velox_core 
    PUBLIC
        whirlpool    
)

target_include_directories(velox_core PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/whirlpool/include  
)