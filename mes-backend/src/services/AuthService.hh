#pragma once

#include <nlohmann/json.hpp>

#include <functional>

// 认证服务 (计划任务 9 / 设计文档 4.2, 5.2 节):
// login / captcha / refresh / logout / profile / password
namespace mes::AuthService {

using JsonCb = std::function<void(const nlohmann::json&)>;
using ErrCb = std::function<void(int code, const std::string& msg)>;

void login(const nlohmann::json& body, const std::string& clientIp, JsonCb onOk, ErrCb onErr);
void refresh(const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void logout(const std::string& sessionId, int64_t refreshExpireIn, JsonCb onOk, ErrCb onErr);
void profile(int64_t userId, JsonCb onOk, ErrCb onErr);
void changePassword(int64_t userId, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void captcha(JsonCb onOk, ErrCb onErr);

} // namespace mes::AuthService
