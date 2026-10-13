cmake_minimum_required(VERSION 3.15)
project(cpp_rate_limiter CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    add_compile_options(/W3 /EHsc)
else()
    add_compile_options(-Wall -Wextra -pthread)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(SOURCES
    src/main.cpp
    src/storage/redis_client.cpp
    src/algorithms/fixed_window.cpp
    src/algorithms/sliding_window_log.cpp
    src/algorithms/sliding_window_counter.cpp
    src/algorithms/token_bucket.cpp
    src/algorithms/leaky_bucket.cpp
)

add_executable(cpp_rate_limiter ${SOURCES})

if(WIN32)
    target_link_libraries(cpp_rate_limiter PRIVATE ws2_32)
else()
    target_link_libraries(cpp_rate_limiter PRIVATE pthread)
endif()
