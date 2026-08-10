cmake_minimum_required(VERSION 3.21)
project(hms-backend CXX)

set(CMAKE_CXX_STANDARD 20)          # Service 层 Drogon 协程要求 C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Drogon CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(bcrypt CONFIG REQUIRED)
find_package(SimpleAmqpClient CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

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
        src/utils/JwtUtils.cc
        src/utils/TimeUtils.cc)
    target_include_directories(hms_unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(hms_unit_tests PRIVATE
        GTest::gtest_main jwt-cpp::jwt-cpp nlohmann_json::nlohmann_json)
    add_test(NAME hms_unit_tests COMMAND hms_unit_tests)
endif()
