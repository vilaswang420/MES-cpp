#pragma once

#include <bcrypt/BCrypt.hpp>
#include <openssl/sha.h>

#include <cstdint>
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

// 快速单向摘要 (仅用于校验缓存键, 避免明文密码驻留缓存): SHA-256 -> hex
inline std::string sha256Hex(const std::string& in) {
    unsigned char buf[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(in.data()), in.size(), buf);
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(SHA256_DIGEST_LENGTH * 2);
    for (unsigned char c : buf) {
        out.push_back(kHex[c >> 4]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

} // namespace mes::CryptoUtils
