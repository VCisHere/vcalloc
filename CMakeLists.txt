cmake_minimum_required(VERSION 3.6)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_COMPILER "/usr/bin/gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

set(CMAKE_CXX_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

project(vcalloc-test C CXX)

add_compile_definitions(VCALLOC_MULTI_THREAD)

add_executable(vcalloc-test
    "./vcalloc/vcalloc.cc"
    "./main.cc"
)

target_include_directories(vcalloc-test 
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# target_link_libraries(misc-test
#         pthread
# )
