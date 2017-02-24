cmake_minimum_required(VERSION 3.6)
project(TcpEventServer)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES main.cpp TcpEventServer.cpp TcpEventServer.h)

add_executable(TcpEventServer ${SOURCE_FILES})

target_link_libraries(TcpEventServer pthread)
target_link_libraries(TcpEventServer event)
