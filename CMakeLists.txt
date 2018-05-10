cmake_minimum_required(VERSION 3.6)
project(base_platform)

#set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_options(-std=c++14)

include_directories(include src)

link_directories(/usr/lib/python3.5/config-3.5m-x86_64-linux-gnu)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-result \
-D__FILENAME__='\"$(notdir $<)\"'")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic \
-Wl,--unresolved-symbols=ignore-in-shared-libs")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")

# library kbase
FILE(GLOB_RECURSE SOURCE_FILES src/*.cpp)

add_library(kbase SHARED ${SOURCE_FILES})

target_link_libraries(kbase z pthread anl)

set_target_properties(kbase PROPERTIES LIBRARY_OUTPUT_DIRECTORY ../lib64)

# tools-registry
add_executable(registry tools/registry/registry.cpp)

target_link_libraries(registry kbase crypto)

set_target_properties(registry PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../tools/runtime/registry/)

# tools-simple_simps
add_executable(simps tools/simple_simps/simple_simps.cpp)

target_link_libraries(simps kbase)

set_target_properties(simps PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../tools/runtime/simple_simps/)

# example-py_plot_ex
add_executable(py_plot_ex examples/py_plot_ex.cpp)

target_link_libraries(py_plot_ex kbase python3.5)

set_target_properties(py_plot_ex PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../examples)

# example-simple_crawler
add_executable(simple_crawler examples/simple_crawler.cpp)

target_link_libraries(simple_crawler kbase)

set_target_properties(simple_crawler PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../examples)

# example-conf_ini
add_executable(conf_ini examples/conf_ini_ex.cpp)

target_link_libraries(conf_ini kbase)

set_target_properties(conf_ini PROPERTIES RUNTIME_OUTPUT_DIRECTORY ../examples)