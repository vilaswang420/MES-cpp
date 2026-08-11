#include <drogon/HttpController.h>

#include "controllers/Common.hh"
#include "services/AuthService.hh"

namespace hms {

// 认证接口 (计划任务 9 / 设计文档 4.2 节 6 接口)
class AuthController : public drogon::HttpController<AuthController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthController::captcha, "/api/v1/auth/captcha", drogon::Get);
    ADD_METHOD_TO(AuthController::refresh, "/api/v1/auth/refresh", drogon::Post);
    ADD_METHOD_TO(AuthController::logout, "/api/v1/auth/logout", drogon::Post);
    ADD_METHOD_TO(AuthController::profile, "/api/v1/auth/profile", drogon::Get);
    ADD_METHOD_TO(AuthController::changePassword, "/api/v1/auth/password", drogon::Put);
    METHOD_LIST_END

    void login(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null()) {
            callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
            return;
        }
        AuthService::login(
            body, req->getPeerAddr().toIp(),
            [callback, traceId](const nlohmann::json& data) {
                callback(ApiResponse::success(data, traceId));
            },
            [callback, traceId](int code, const std::string& msg) {
                callback(ApiResponse::error(code, msg, traceId));
            });
    }

    void captcha(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        AuthService::captcha(
            [callback, traceId](const nlohmann::json& data) {
                callback(ApiResponse::success(data, traceId));
            },
            [callback, traceId](int code, const std::string& msg) {
                callback(ApiResponse::error(code, msg, traceId));
            });
    }

    void refresh(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null()) {
            callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
            return;
        }
        AuthService::refresh(
            body,
            [callback, traceId](const nlohmann::json& data) {
                callback(ApiResponse::success(data, traceId));
            },
            [callback, traceId](int code, const std::string& msg) {
                callback(ApiResponse::error(code, msg, traceId));
            });
    }

    void logout(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto attrs = req->getAttributes();
        std::string sessionId;
        if (attrs->find("session_id"))
            sessionId = attrs->get<std::string>("session_id");
        if (sessionId.empty()) {
            callback(ApiResponse::error(401, "会话不存在", traceId));
            return;
        }
        // 黑名单 TTL 覆盖 refresh_token 最大寿命 (7 天)
        AuthService::logout(
            sessionId, 7 * 24 * 3600,
            [callback, traceId](const nlohmann::json& data) {
                callback(ApiResponse::success(data, traceId));
            },
            [callback, traceId](int code, const std::string& msg) {
                callback(ApiResponse::error(code, msg, traceId));
            });
    }

    void profile(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto ctx = userCtxOf(req);
        AuthService::profile(
            ctx.userId,
            [callback, traceId](const nlohmann::json& data) {
                callback(ApiResponse::success(data, traceId));
            },
            [callback, traceId](int code, const std::string& msg) {
                callback(ApiResponse::error(code, msg, traceId));
            });
    }

    void changePassword(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null()) {
            callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
            return;
        }
        auto ctx = userCtxOf(req);
        AuthService::changePassword(
            ctx.userId, body,
            [callback, traceId](const nlohmann::json& data) {
                callback(ApiResponse::success(data, traceId));
            },
            [callback, traceId](int code, const std::string& msg) {
                callback(ApiResponse::error(code, msg, traceId));
            });
    }
};

} // namespace hms
