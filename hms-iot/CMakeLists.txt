cmake_minimum_required(VERSION 3.21)
project(hms-iot CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SimpleAmqpClient CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# P4-5.1: Linux 编译全部源 (含 ModbusClient/DevicePoller/ConfigLoader/CmdConsumer);
# Windows 仅编译 main.cc 骨架 (POSIX socket 不可用)
if(WIN32)
    set(IOT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc")
else()
    file(GLOB_RECURSE IOT_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
endif()

add_executable(hms-iot ${IOT_SOURCES})
target_include_directories(hms-iot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hms-iot PRIVATE
    SimpleAmqpClient::SimpleAmqpClient
    nlohmann_json::nlohmann_json)

# M2 (计划任务 18): 并发模型 = epoll 事件循环 + 协议 worker 线程池;
# 采集线程就地缩放/校验, 发布线程批量 publish (>=100 条或 100ms 窗口 + publisher confirms)。
# Linux 目标平台; Windows 仅用于本地模拟器联调 (healthz 部分)。
