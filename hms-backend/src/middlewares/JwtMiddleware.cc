#include "middlewares/JwtMiddleware.hh"

#include <drogon/drogon.h>

namespace hms {

namespace {
void sendUnauthorized(const drogon::MiddlewareCallback& cb, const std::string& msg,
                      const std::string& traceId) {
    cb(ApiResponse::error(401, msg, traceId));
}
} // namespace

void JwtMiddleware::invoke(const drogon::HttpRequestPtr& req, MiddlewareNextCallback&& nextCb,
                           MiddlewareCallback&& mcb) {
    const auto path = req->path();
    const auto method = req->methodString();
    const auto traceId = traceIdOf(req);

    // 1. 公开白名单直接放行
    if (PermRoutes::isPublicPath(path, method)) {
        nextCb(req, std::move(mcb));
        return;
    }

    // 1.1 refresh 接口以 refresh_token 自证身份 (典型场景即 access 已过期),
    // 豁免 access token 校验; 权限层仍要求 auth:bearer 映射存在
    if (path == "/api/v1/auth/refresh" && std::string(method) == "POST") {
        nextCb(req, std::move(mcb));
        return;
    }

    // 2. 提取 Bearer Token
    auto authHeader = req->getHeader("Authorization");
    if (authHeader.size() <= 7 || authHeader.substr(0, 7) != "Bearer ") {
        sendUnauthorized(mcb, "缺少认证令牌", traceId);
        return;
    }
    std::string token = authHeader.substr(7);

    // 3. 验证 JWT 签名与过期
    auto payload = JwtUtils::verifyAccessToken(token);
    if (!payload) {
        sendUnauthorized(mcb, "令牌无效或已过期", traceId);
        return;
    }

    // 4. 检查 Redis 黑名单 (会话注销后失效)
    auto rdb = drogon::app().getRedisClient();
    auto sessionId = payload->sessionId;
    rdb->execCommandAsync(
        [req, payload, nextCb = std::move(nextCb), mcb = std::move(mcb),
         traceId](const drogon::nosql::RedisResult& result) mutable {
            // EXISTS 返回 1 表示会话已注销
            if (result.asInteger() > 0) {
                sendUnauthorized(mcb, "会话已注销", traceId);
                return;
            }
            // 5. 注入用户上下文 (合成后的 data_scope, 见 5.4)
            auto attrs = req->getAttributes();
            attrs->insert("current_user_id", payload->userId);
            attrs->insert("current_username", payload->username);
            attrs->insert("current_dept_id", payload->deptId);
            attrs->insert("current_roles", payload->roles);
            attrs->insert("data_scope", payload->dataScope);
            attrs->insert("custom_dept_ids", payload->customDeptIds);
            attrs->insert("session_id", payload->sessionId);
            nextCb(req, std::move(mcb));
        },
        [mcb, traceId](const std::exception& e) {
            // Redis 异常时 fail-closed: 拒绝而非放行
            LOG_ERROR << "redis blacklist check failed: " << e.what();
            sendUnauthorized(mcb, "认证服务暂不可用", traceId);
        },
        "EXISTS jwt:blacklist:%s", sessionId);
}

} // namespace hms
