# 第三方 bcrypt (trusch/libbcrypt, MIT 许可, 见 LICENSE) vendored 构建。
# vcpkg 主仓无 bcrypt port, 故以源码方式内置; 仅编译纯 C 文件 (x86.S 为 GCC 汇编, MSVC 走 C 后备分支)。
cmake_minimum_required(VERSION 3.21)
project(mes_bcrypt C)

add_library(mes_bcrypt STATIC
    src/bcrypt.c
    src/crypt_blowfish.c
    src/crypt_gensalt.c
    src/wrapper.c)

# 注意双 include 路径: 源码平台分支不同 —— Windows 走相对路径 ../include/bcrypt/bcrypt.h,
# Linux 分支为裸 #include "bcrypt.h"/"ow-crypt.h", 需将 include/bcrypt 加入搜索路径
target_include_directories(mes_bcrypt PUBLIC include include/bcrypt)

if(MSVC)
    # 第三方 C 源码含 strcpy 等 CRT 安全告警, 不升级告警级别, 仅静默此类警告
    target_compile_definitions(mes_bcrypt PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_library(bcrypt::bcrypt ALIAS mes_bcrypt)
