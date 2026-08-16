cmake_minimum_required(VERSION 3.21)
project(hms-iot CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: 源码为 UTF-8 (含中文注释/字符串), 显式声明避免按 GBK 解析导致 C2001/C1071
if(MSVC)
    add_compile_options(/utf-8)
endif()

# SimpleAmqpClient: vcpkg port 只提供 pkg-config 无 CMake Config, 手工建 INTERFACE target
# (与 hms-backend/CMakeLists.txt 同一方案)
# Windows vcpkg 动态链接导入库带版本后缀: SimpleAmqpClient.7.lib / rabbitmq.4.lib
# Linux vcpkg 静态/动态库无版本后缀: libSimpleAmqpClient.a / librabbitmq.a
if(WIN32)
    find_library(SIMPLEAMQPCLIENT_LIB NAMES SimpleAmqpClient.7 REQUIRED)
    find_library(RABBITMQ_LIB NAMES rabbitmq.4 REQUIRED)
else()
    find_library(SIMPLEAMQPCLIENT_LIB NAMES SimpleAmqpClient REQUIRED)
    find_library(RABBITMQ_LIB NAMES rabbitmq REQUIRED)
endif()
add_library(SimpleAmqpClient_SAC INTERFACE)
target_include_directories(SimpleAmqpClient_SAC INTERFACE
    "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include")
target_link_libraries(SimpleAmqpClient_SAC INTERFACE
    ${SIMPLEAMQPCLIENT_LIB}
    ${RABBITMQ_LIB})
if(WIN32)
    target_link_libraries(SimpleAmqpClient_SAC INTERFACE ws2_32)
endif()
add_library(SimpleAmqpClient::SimpleAmqpClient ALIAS SimpleAmqpClient_SAC)
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
