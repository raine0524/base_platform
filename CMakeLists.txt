cmake_minimum_required(VERSION 3.5)
project(base_platform)

#set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_options(-std=c++11)

set(GOOGLETEST_VERSION 1.8.0)
message("googletest version: ${GOOGLETEST_VERSION}")

include_directories(
        include cpp
        extras/googletest/${GOOGLETEST_VERSION}/include
        unit_test/sys_hook
)

link_directories(extras/googletest/${GOOGLETEST_VERSION}/lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wno-unused-result")
message("compile flags: ${CMAKE_CXX_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic \
-Wl,--unresolved-symbols=ignore-in-shared-libs")
message("binary linker flags: ${CMAKE_EXE_LINKER_FLAGS}")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
message("shared linker flags: ${CMAKE_SHARED_LINKER_FLAGS}")

# library kbase
set(LIB_NAME kbase)
set(LIB_VERSION 1.0.0)
message("library name: ${LIB_NAME} version: ${LIB_VERSION}")

FILE(GLOB_RECURSE SOURCE_FILES cpp/*.cpp)
add_library(${LIB_NAME} SHARED ${SOURCE_FILES})
target_link_libraries(${LIB_NAME} z pthread anl crypto)
set_target_properties(${LIB_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib64)

# 构建hook library,从而提供mock system object的能力来支持一些功能性接口的测试
set(TESTS_ROOT_DIR unit_test)
set(HOOK_LIB_NAME sys_hook)
set(HOOKLIB_BIN_DIR ${PROJECT_BINARY_DIR}/${TESTS_ROOT_DIR}/${HOOK_LIB_NAME})

FILE(GLOB_RECURSE HOOK_SRCS ${TESTS_ROOT_DIR}/${HOOK_LIB_NAME}/*.cpp)
add_library(${HOOK_LIB_NAME} SHARED ${HOOK_SRCS})
target_link_libraries(${HOOK_LIB_NAME})
set_target_properties(${HOOK_LIB_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${HOOKLIB_BIN_DIR})

# 构建kbase相关测试用例
enable_testing()
set(KBASETEST_BIN_DIR ${PROJECT_BINARY_DIR}/${TESTS_ROOT_DIR}/${LIB_NAME})
FILE(GLOB_RECURSE KBASETEST_SRC "${TESTS_ROOT_DIR}/${LIB_NAME}/*.cpp")
foreach(FILE_PATH ${KBASETEST_SRC})
    string(REGEX REPLACE ".+/(.+)\\..*" "\\1" FILE_NAME ${FILE_PATH})   #获取unit_test/kbase目录下cpp源文件的名称
    add_executable(${FILE_NAME} ${FILE_PATH})
    target_link_libraries(${FILE_NAME} ${LIB_NAME} ${HOOK_LIB_NAME} gtest gmock)
    set_target_properties(${FILE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${KBASETEST_BIN_DIR})
    add_test(NAME ${FILE_NAME} COMMAND ${KBASETEST_BIN_DIR}/${FILE_NAME})
    set_property(TEST ${FILE_NAME} PROPERTY ENVIRONMENT LD_PRELOAD=${HOOKLIB_BIN_DIR}/lib${HOOK_LIB_NAME}.so)
endforeach()

# 通过测试用例后,安装基础库
install(DIRECTORY include/ DESTINATION ${PROJECT_SOURCE_DIR}/deploy/depends/${LIB_NAME}/${LIB_VERSION}/include)
set(LIBRARY_INSTALL_PATH ${PROJECT_SOURCE_DIR}/deploy/depends/${LIB_NAME}/${LIB_VERSION}/lib)
install(TARGETS ${LIB_NAME} LIBRARY DESTINATION ${LIBRARY_INSTALL_PATH})

# 安装googletest, cpp相关的项目都使用其作为测试框架
set(GOOGLETEST_DIR_NAME googletest)
install(DIRECTORY extras/${GOOGLETEST_DIR_NAME}/ DESTINATION ${PROJECT_SOURCE_DIR}/deploy/depends/${GOOGLETEST_DIR_NAME})

set(CMAKE_INSTALL_RPATH ${LIBRARY_INSTALL_PATH})
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# examples, 安装时将编译的二进制文件以及源码一起打包发布，开发者通过examples以及其他文档迅速上手该框架及相关工具
set(EXAMPLES_DIR_NAME examples)
set(EXAMPLES_BIN_DIR ${PROJECT_BINARY_DIR}/${EXAMPLES_DIR_NAME})
FILE(GLOB_RECURSE EXS_SRC "${EXAMPLES_DIR_NAME}/*.cpp")
foreach(FILE_PATH ${EXS_SRC})
    string(REGEX REPLACE ".+/(.+)\\..*" "\\1" FILE_NAME ${FILE_PATH})   #获取examples目录下cpp源文件的名称
    add_executable(${FILE_NAME} ${FILE_PATH})
    target_link_libraries(${FILE_NAME} ${LIB_NAME})
    set_target_properties(${FILE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXAMPLES_BIN_DIR})
    install(PROGRAMS ${EXAMPLES_BIN_DIR}/${FILE_NAME} DESTINATION ${PROJECT_SOURCE_DIR}/deploy/${EXAMPLES_DIR_NAME})
endforeach()
install(DIRECTORY ${EXAMPLES_DIR_NAME}/ DESTINATION ${PROJECT_SOURCE_DIR}/deploy/${EXAMPLES_DIR_NAME})
