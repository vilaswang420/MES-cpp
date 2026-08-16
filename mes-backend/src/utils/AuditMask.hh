#pragma once

#include <nlohmann/json.hpp>
#include <string>

// 审计请求参数脱敏 (P3-4.5 严重, 纯逻辑 header-only 可单测):
// 登录/改密等请求的 body 含明文密码, 原实现直接截取 body 前 2KB 落库
// sys_audit_logs.request_params —— 审计库被拖库 = 密码批量泄露。
// 本模块对写操作 body 做 JSON 解析, 将 key 含 "password" (大小写不敏感)
// 的字段值替换为掩码, 保留其余字段与审计完整性。
// 解析失败 (非 JSON 或损坏) 时原样返回, 不阻断审计 (fail-open 保审计可用)。
namespace mes::AuditMask {

inline constexpr const char* kMask = "***";

namespace detail {

inline bool isPasswordKey(const std::string& key) {
    // key 含 password (忽略大小写): 覆盖 password/newPassword/oldPassword/confirmPassword
    if (key.size() < 8)
        return false;
    for (size_t i = 0; i + 8 <= key.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < 8; ++j) {
            char c = key[i + j];
            char p = "password"[j];
            if (std::tolower(static_cast<unsigned char>(c)) != p)
                match = false;
        }
        if (match)
            return true;
    }
    return false;
}

inline void maskObject(nlohmann::json& obj) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.value().is_object()) {
            maskObject(it.value()); // 嵌套对象递归
        } else if (isPasswordKey(it.key())) {
            it.value() = kMask;
        }
    }
}

} // namespace detail

inline std::string maskSensitiveJson(const std::string& body) {
    if (body.empty())
        return body;
    try {
        auto j = nlohmann::json::parse(body);
        if (j.is_object()) {
            detail::maskObject(j);
            return j.dump();
        }
        return body; // 非 object (数组/标量) 原样
    } catch (const nlohmann::json::exception&) {
        return body; // 非 JSON 原样 (审计不阻断)
    }
}

} // namespace mes::AuditMask
