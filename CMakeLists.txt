cmake_minimum_required(VERSION 3.6)
project(kbase)

#set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_options(-std=c++11)

set(SOURCE_FILES
        kbase/base_util.cpp
        kbase/console_impl.cpp
        kbase/epoll_thread_impl.cpp
        kbase/epoll_util_impl.cpp
        kbase/evd_thread_impl.cpp
        kbase/py_wrapper_impl.cpp
        kbase/serialize_impl.cpp
        kbase/statis_impl.cpp
        kbase/tinyxml2.cpp
        kbase/xml_impl.cpp)

include_directories(include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-result")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")

set(LIBRARY_OUTPUT_PATH lib64)

add_library(kbase SHARED ${SOURCE_FILES})

target_link_libraries(kbase z pthread)