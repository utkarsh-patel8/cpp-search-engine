cmake_minimum_required(VERSION 3.16)

project(
    cpp_search_engine
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(
    search_engine
    src/main.cpp
    src/search_engine.cpp
    src/self_tests.cpp
)

add_executable(
    benchmark_generator
    benchmark_generator.cpp
)

target_include_directories(
    search_engine
    PRIVATE
    include
)

target_link_libraries(
    search_engine
    PRIVATE
    Threads::Threads
)

if(MSVC)
    target_compile_options(
        search_engine
        PRIVATE
        /W4
    )
else()
    target_compile_options(
        search_engine
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

enable_testing()

add_test(
    NAME search_engine_tests
    COMMAND search_engine --test
)