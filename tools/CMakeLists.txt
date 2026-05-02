add_executable(create_itch create_itch.cpp)
add_executable(itch_parser itch_parser.cpp)

target_link_libraries(create_itch velox_core)
target_link_libraries(itch_parser velox_core)

target_include_directories(create_itch PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(itch_parser PRIVATE ${CMAKE_SOURCE_DIR}/include)
