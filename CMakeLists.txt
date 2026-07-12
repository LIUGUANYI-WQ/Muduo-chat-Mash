cmake_minimum_required(VERSION 3.0)
project(MuduoChat C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MUDUO_DIR "${CMAKE_SOURCE_DIR}/build2/release-cpp11")
set(MUDUO_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/muduo")
set(MUDUO_LIB_DIR "${MUDUO_DIR}/lib")

include_directories(
    ${MUDUO_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/proto
    ${PROTOBUF_INCLUDE_DIR}
)

link_directories(${MUDUO_LIB_DIR})

find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})

add_library(chat_proto STATIC
    proto/chat.pb.cc
)
target_include_directories(chat_proto PUBLIC ${CMAKE_SOURCE_DIR}/proto)
target_link_libraries(chat_proto ${PROTOBUF_LIBRARY})

add_subdirectory(src)
add_subdirectory(client)
