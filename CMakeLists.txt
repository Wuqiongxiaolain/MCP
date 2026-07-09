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

# On MinGW, static-link the runtime so the exe has no external DLL dependency
# (MCP clients may spawn it with a stripped PATH).
if(MINGW)
    target_link_options(graphmcp PRIVATE -static -static-libgcc -static-libstdc++)
endif()

enable_testing()

add_executable(graphmcp_tests tests/test_main.cpp)
target_include_directories(graphmcp_tests PRIVATE src)
add_test(NAME unit_tests COMMAND graphmcp_tests)

add_executable(graphmcp_version_tests tests/test_version.cpp)
target_include_directories(graphmcp_version_tests PRIVATE src)
add_test(NAME version_tests COMMAND graphmcp_version_tests)

add_executable(graphmcp_cursor_tests tests/test_cursor.cpp)
target_include_directories(graphmcp_cursor_tests PRIVATE src)
add_test(NAME cursor_tests COMMAND graphmcp_cursor_tests)

install(TARGETS graphmcp RUNTIME DESTINATION bin)
