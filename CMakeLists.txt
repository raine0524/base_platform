cmake_minimum_required(VERSION 3.6)
project(kbase)

#set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_options(-std=c++11)

FILE(GLOB_RECURSE SOURCE_FILES kbase/*.cpp)

include_directories(include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-result")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")

set(LIBRARY_OUTPUT_PATH ../lib64)

add_library(kbase SHARED ${SOURCE_FILES})

target_link_libraries(kbase z pthread)