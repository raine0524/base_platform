cmake_minimum_required(VERSION 3.6)
project(base_platform)

#set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_options(-std=c++14)

include_directories(include src)

link_directories(/usr/lib/python3.5/config-3.5m-x86_64-linux-gnu)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wno-unused-result \
-D__FILENAME__='\"$(notdir $<)\"'")
message("compile flags: ${CMAKE_CXX_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic \
-Wl,--unresolved-symbols=ignore-in-shared-libs")
message("binary linker flags: ${CMAKE_EXE_LINKER_FLAGS}")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
message("shared linker flags: ${CMAKE_SHARED_LINKER_FLAGS}")

# library kbase
set(LIB_NAME kbase)
set(LIB_VERSION 1.0.0)
message("library name: ${KBASE_NAME} version: ${KBASE_VERSION}")

FILE(GLOB_RECURSE SOURCE_FILES src/*.cpp)
add_library(${LIB_NAME} SHARED ${SOURCE_FILES})
target_link_libraries(${LIB_NAME} z pthread anl crypto)
set_target_properties(${LIB_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib64)

# 安装基础库
install(DIRECTORY include/ DESTINATION ${PROJECT_SOURCE_DIR}/deploy/depends/${LIB_NAME}/${LIB_VERSION}/include)
set(LIBRARY_INSTALL_PATH ${PROJECT_SOURCE_DIR}/deploy/depends/${LIB_NAME}/${LIB_VERSION}/lib)
install(TARGETS ${LIB_NAME} LIBRARY DESTINATION ${LIBRARY_INSTALL_PATH})

set(CMAKE_INSTALL_RPATH ${LIBRARY_INSTALL_PATH})
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# tools, 安装时将源码目录的配置文件也一起安装到指定的部署目录
set(TOOLS_DIR_NAME tools)
set(TOOLS_BIN_DIR ${PROJECT_BINARY_DIR}/${TOOLS_DIR_NAME})
FILE(GLOB_RECURSE TOOL_SRC "${TOOLS_DIR_NAME}/*.cpp")
foreach(FILE_PATH ${TOOL_SRC})
    string(REGEX REPLACE ".+/(.+)\\..*" "\\1" FILE_NAME ${FILE_PATH})   #获取tools目录下cpp源文件的名称
    add_executable(${FILE_NAME} ${FILE_PATH})
    target_link_libraries(${FILE_NAME} ${LIB_NAME})
    set_target_properties(${FILE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TOOLS_BIN_DIR})

    FILE(GLOB CONFIG_FILES ${TOOLS_DIR_NAME}/${FILE_NAME}/*.ini ${TOOLS_DIR_NAME}/${FILE_NAME}/*.xml)
    install(PROGRAMS ${TOOLS_BIN_DIR}/${FILE_NAME} DESTINATION ${PROJECT_SOURCE_DIR}/deploy/toolkit/${FILE_NAME})
    install(FILES ${CONFIG_FILES} DESTINATION ${PROJECT_SOURCE_DIR}/deploy/toolkit/${FILE_NAME}/ini)
endforeach()
FILE(GLOB RUN_SCRIPTS ${TOOLS_DIR_NAME}/*.sh)
install(PROGRAMS ${RUN_SCRIPTS} DESTINATION ${PROJECT_SOURCE_DIR}/deploy/toolkit)

# examples, 安装时将编译的二进制文件以及源码一起打包发布，开发者通过examples以及其他文档迅速上手该框架及相关工具
set(EXAMPLES_DIR_NAME examples)
set(EXAMPLES_BIN_DIR ${PROJECT_BINARY_DIR}/${EXAMPLES_DIR_NAME})
FILE(GLOB_RECURSE EXS_SRC "${EXAMPLES_DIR_NAME}/*.cpp")
foreach(FILE_PATH ${EXS_SRC})
    string(REGEX REPLACE ".+/(.+)\\..*" "\\1" FILE_NAME ${FILE_PATH})   #获取examples目录下cpp源文件的名称
    add_executable(${FILE_NAME} ${FILE_PATH})
    target_link_libraries(${FILE_NAME} ${LIB_NAME} python3.5)
    set_target_properties(${FILE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXAMPLES_BIN_DIR})
    install(PROGRAMS ${EXAMPLES_BIN_DIR}/${FILE_NAME} DESTINATION ${PROJECT_SOURCE_DIR}/deploy/${EXAMPLES_DIR_NAME})
endforeach()
install(DIRECTORY ${EXAMPLES_DIR_NAME}/ DESTINATION ${PROJECT_SOURCE_DIR}/deploy/${EXAMPLES_DIR_NAME})