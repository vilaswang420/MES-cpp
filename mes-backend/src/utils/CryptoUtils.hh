#pragma once

#include <bcrypt/BCrypt.hpp>

#include <string>

// 密码哈希: bcrypt (设计文档 5.2 节第 4 步)
namespace mes::CryptoUtils {

inline std::string hashPassword(const std::string& plain) {
    // cost=10 与 seed 数据一致; 生产可调至 12
    return BCrypt::generateHash(plain, 10);
}

inline bool verifyPassword(const std::string& plain, const std::string& hash) {
    try {
        return BCrypt::validatePassword(plain, hash);
    } catch (...) {
        return false;
    }
}

} // namespace mes::CryptoUtils
