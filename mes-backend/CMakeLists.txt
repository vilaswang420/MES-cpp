cmake_minimum_required(VERSION 3.21)
project(mes-backend CXX)

set(CMAKE_CXX_STANDARD 20)          # Service 层 Drogon 协程要求 C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GCC 13 在 C++20 协程 (drogon::Task<>) 下存在内部编译器错误 (ICE):
#   internal compiler error: in build_special_member_call, at cp/call.cc:11096
#   触发文件: src/services/IntegrationService.cc (reportCompletionSaga 函数)
# 解决方案: 使用 GCC 14+ 编译 (build_cn.sh 中已固定 CC=gcc-14 CXX=g++-14)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(WARNING "GCC ${CMAKE_CXX_COMPILER_VERSION} 检测到已知 C++20 协程 ICE bug, "
                    "建议使用 GCC 14+ (apt install gcc-14 g++-14, "
                    "cmake -DCMAKE_CXX_COMPILER=g++-14)")
endif()

# MSVC: 源码为 UTF-8 (含中文注释/字符串), 显式声明避免按 GBK 解析导致 C2001
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Drogon CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
# SimpleAmqpClient: vcpkg port 只提供 pkg-config 无 CMake Config, 手工建 INTERFACE target
# Windows vcpkg 动态链接导入库带版本后缀: SimpleAmqpClient.7.lib / rabbitmq.4.lib
# Linux vcpkg 静态/动态库无版本后缀: libSimpleAmqpClient.a / librabbitmq.a
if(WIN32)
    find_library(SIMPLEAMQPCLIENT_LIB NAMES SimpleAmqpClient.7 REQUIRED)
    find_library(SIMPLEAMQPCLIENT_LIB_DEBUG NAMES SimpleAmqpClient.7
        PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
    find_library(RABBITMQ_LIB NAMES rabbitmq.4 REQUIRED)
    find_library(RABBITMQ_LIB_DEBUG NAMES rabbitmq.4
        PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
else()
    find_library(SIMPLEAMQPCLIENT_LIB NAMES SimpleAmqpClient REQUIRED)
    find_library(RABBITMQ_LIB NAMES rabbitmq REQUIRED)
    set(SIMPLEAMQPCLIENT_LIB_DEBUG "${SIMPLEAMQPCLIENT_LIB}")
    set(RABBITMQ_LIB_DEBUG "${RABBITMQ_LIB}")
endif()
add_library(SimpleAmqpClient_SAC INTERFACE)
target_include_directories(SimpleAmqpClient_SAC INTERFACE
    "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include")
target_link_libraries(SimpleAmqpClient_SAC INTERFACE
    "$<IF:$<CONFIG:Debug>,${SIMPLEAMQPCLIENT_LIB_DEBUG},${SIMPLEAMQPCLIENT_LIB}>"
    "$<IF:$<CONFIG:Debug>,${RABBITMQ_LIB_DEBUG},${RABBITMQ_LIB}>")
if(WIN32)
    target_link_libraries(SimpleAmqpClient_SAC INTERFACE ws2_32)
endif()
add_library(SimpleAmqpClient::SimpleAmqpClient ALIAS SimpleAmqpClient_SAC)
find_package(nlohmann_json CONFIG REQUIRED)

# bcrypt: vcpkg 主仓无此 port, 以 vendored 源码内置 (third_party/bcrypt, MIT)
add_subdirectory(third_party/bcrypt)

file(GLOB_RECURSE MES_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

add_executable(mes-backend ${MES_SOURCES})
target_include_directories(mes-backend PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mes-backend PRIVATE
    Drogon::Drogon
    hiredis::hiredis
    jwt-cpp::jwt-cpp
    bcrypt::bcrypt
    SimpleAmqpClient::SimpleAmqpClient
    nlohmann_json::nlohmann_json)

# ---- 单元测试 (GTest) ----
option(MES_BUILD_TESTS "Build unit tests" ON)
if(MES_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    # 纯逻辑单测不链接 Drogon, 只测可独立编译的工具/状态机
    add_executable(mes_unit_tests
        tests/test_work_order_state_machine.cc
        tests/test_data_scope_filter.cc
        tests/test_jwt_utils.cc
        tests/test_bcrypt.cc
        tests/test_circuit_breaker.cc
        tests/test_outbox_policy.cc
        tests/test_service_rules.cc
        tests/test_audit_mask.cc
        tests/test_captcha.cc
        tests/test_oee_calc.cc
        src/utils/JwtUtils.cc
        src/utils/TimeUtils.cc)
    target_include_directories(mes_unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(mes_unit_tests PRIVATE
        GTest::gtest_main jwt-cpp::jwt-cpp nlohmann_json::nlohmann_json bcrypt::bcrypt)
    add_test(NAME mes_unit_tests COMMAND mes_unit_tests)
endif()
