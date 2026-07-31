add_library(velox_core STATIC
    # Feed handlers
    feed/feed_handler.cpp
    
    # Order book
    book/order_book.cpp
    book/price_level.cpp
    book/book_snapshot.cpp
    
    # Matching
    matching/matching_engine.cpp
    
    # Risk
    risk/risk_manager.cpp
    risk/position_manager.cpp
    
    # Gateway
    gateway/execution_gateway.cpp
    gateway/fix_encoder.cpp

    # Strategy
    sim/strategy/bot_manager.cpp
    sim/strategy/bot.cpp

    # Environment
    sim/env/market_sim.cpp
)

target_link_libraries(velox_core
    PUBLIC
        whirlpool::whirlpool
)

target_include_directories(velox_core PUBLIC 
    ${CMAKE_SOURCE_DIR}/include 
)

add_executable(velox_trading_engine main.cpp)

target_link_libraries(velox_trading_engine
    velox_core
)

target_include_directories(velox_trading_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)