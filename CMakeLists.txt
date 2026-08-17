cmake_minimum_required(VERSION 3.20)

project(gbemu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gbcore
    src/Bus.cpp
    src/Cpu.cpp
    src/rom.cpp
)

target_include_directories(gbcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(gbemu
    src/main.cpp
)

target_link_libraries(gbemu PRIVATE gbcore)

add_executable(gbemu_tests
    tests/core_test.cpp
)

target_link_libraries(gbemu_tests PRIVATE gbcore)