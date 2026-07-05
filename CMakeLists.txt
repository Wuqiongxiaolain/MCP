cmake_minimum_required(VERSION 3.14)
project(graphmcp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_executable(graphmcp src/main.cpp)
target_include_directories(graphmcp PRIVATE src)

enable_testing()
add_executable(graphmcp_tests tests/test_main.cpp)
target_include_directories(graphmcp_tests PRIVATE src)
add_test(NAME unit_tests COMMAND graphmcp_tests)

install(TARGETS graphmcp RUNTIME DESTINATION bin)
