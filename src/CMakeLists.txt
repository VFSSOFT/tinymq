cmake_minimum_required(VERSION 3.23)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE SRC_FILES ${SRC_DIR}/*.c ${SRC_DIR}/*.h)

add_library(tinymq_lib STATIC ${SRC_FILES})

include_directories(../include)

target_link_libraries(tinymq_lib PRIVATE)
