cmake_minimum_required(VERSION 3.21)
project(hms-backend CXX)

set(CMAKE_CXX_STANDARD 20)          # Service 层 Drogon 协程要求 C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC: 源码为 UTF-8 (含中文注释/字符串), 显式声明避免按 GBK 解析导致 C2001
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Drogon CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
# SimpleAmqpClient: vcpkg port 只提供 pkg-config 无 CMake Config, 手工建 INTERFACE target
# 动态链接导入库带版本后缀: SimpleAmqpClient.7.lib / rabbitmq.4.lib
find_library(SIMPLEAMQPCLIENT_LIB NAMES SimpleAmqpClient.7 REQUIRED)
find_library(SIMPLEAMQPCLIENT_LIB_DEBUG NAMES SimpleAmqpClient.7 PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
find_library(RABBITMQ_LIB NAMES rabbitmq.4 REQUIRED)
find_library(RABBITMQ_LIB_DEBUG NAMES rabbitmq.4 PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
add_library(SimpleAmqpClient_SAC INTERFACE)
target_include_directories(SimpleAmqpClient_SAC INTERFACE
    "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include")
target_link_libraries(SimpleAmqpClient_SAC INTERFACE
    "$<IF:$<CONFIG:Debug>,${SIMPLEAMQPCLIENT_LIB_DEBUG},${SIMPLEAMQPCLIENT_LIB}>"
    "$<IF:$<CONFIG:Debug>,${RABBITMQ_LIB_DEBUG},${RABBITMQ_LIB}>"
    ws2_32)
add_library(SimpleAmqpClient::SimpleAmqpClient ALIAS SimpleAmqpClient_SAC)
find_package(nlohmann_json CONFIG REQUIRED)

# bcrypt: vcpkg 主仓无此 port, 以 vendored 源码内置 (third_party/bcrypt, MIT)
add_subdirectory(third_party/bcrypt)

file(GLOB_RECURSE HMS_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

add_executable(hms-backend ${HMS_SOURCES})
target_include_directories(hms-backend PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hms-backend PRIVATE
    Drogon::Drogon
    hiredis::hiredis
    jwt-cpp::jwt-cpp
    bcrypt::bcrypt
    SimpleAmqpClient::SimpleAmqpClient
    nlohmann_json::nlohmann_json)

# ---- 单元测试 (GTest) ----
option(HMS_BUILD_TESTS "Build unit tests" ON)
if(HMS_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    # 纯逻辑单测不链接 Drogon, 只测可独立编译的工具/状态机
    add_executable(hms_unit_tests
        tests/test_work_order_state_machine.cc
        tests/test_data_scope_filter.cc
        tests/test_jwt_utils.cc
        tests/test_bcrypt.cc
        tests/test_circuit_breaker.cc
        src/utils/JwtUtils.cc
        src/utils/TimeUtils.cc)
    target_include_directories(hms_unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(hms_unit_tests PRIVATE
        GTest::gtest_main jwt-cpp::jwt-cpp nlohmann_json::nlohmann_json bcrypt::bcrypt)
    add_test(NAME hms_unit_tests COMMAND hms_unit_tests)
endif()
