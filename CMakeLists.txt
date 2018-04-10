cmake_minimum_required(VERSION 3.6)
project(base_platform)

#set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_options(-std=c++11)

include_directories(include)

link_directories(/usr/lib/python3.5/config-3.5m-x86_64-linux-gnu)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-result")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic \
-Wl,--unresolved-symbols=ignore-in-shared-libs")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")

# library kbase
FILE(GLOB_RECURSE SOURCE_FILES kbase/*.cpp)

add_library(kbase SHARED ${SOURCE_FILES})

target_link_libraries(kbase z pthread anl)

set_target_properties(kbase PROPERTIES LIBRARY_OUTPUT_DIRECTORY ../lib64)

# example-py_plot_ex
add_executable(py_plot_ex examples/py_plot_ex.cpp)

target_link_libraries(py_plot_ex kbase python3.5)

set_target_properties(py_plot_ex PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../examples)

# example-simple_crawler
add_executable(simple_crawler examples/simple_crawler.cpp)

target_link_libraries(simple_crawler kbase)

set_target_properties(simple_crawler PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../examples)

# example-sch_ecs_ex
add_executable(sch_ecs_ex examples/sch_ecs_ex.cpp)

target_link_libraries(sch_ecs_ex kbase)

set_target_properties(sch_ecs_ex PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../examples)