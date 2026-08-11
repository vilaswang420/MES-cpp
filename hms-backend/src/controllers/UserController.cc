#include <drogon/HttpController.h>

#include "controllers/Common.hh"
#include "services/SystemService.hh"

namespace hms {

// 用户管理 (计划任务 11 / 设计文档 4.3 节)
class UserController : public drogon::HttpController<UserController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::list, "/api/v1/system/users", drogon::Get);
    ADD_METHOD_TO(UserController::create, "/api/v1/system/users", drogon::Post);
    ADD_METHOD_TO(UserController::get, "/api/v1/system/users/{id}", drogon::Get);
    ADD_METHOD_TO(UserController::update, "/api/v1/system/users/{id}", drogon::Put);
    ADD_METHOD_TO(UserController::remove, "/api/v1/system/users/{id}", drogon::Delete);
    ADD_METHOD_TO(UserController::resetPassword, "/api/v1/system/users/{id}/reset-password",
                  drogon::Put);
    ADD_METHOD_TO(UserController::setStatus, "/api/v1/system/users/{id}/status", drogon::Put);
    ADD_METHOD_TO(UserController::assignRoles, "/api/v1/system/users/{id}/roles", drogon::Put);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        SystemService::listUsers(paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                                 paramStr(req, "keyword"), paramInt(req, "status", -1),
                                 okCb(callback, traceId), errCb(callback, traceId));
    }

    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        SystemService::getUser(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void create(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        SystemService::createUser(body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void update(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        SystemService::updateUser(id, body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void remove(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        SystemService::deleteUser(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void resetPassword(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        SystemService::resetPassword(id, body ? body : nlohmann::json::object(),
                                     notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void setStatus(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("status"))
            return callback(ApiResponse::error(400, "status 必填", traceId));
        SystemService::setUserStatus(id, body["status"].get<int>(), notNullCb(callback, traceId),
                                     errCb(callback, traceId));
    }

    void assignRoles(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("role_ids") || !body["role_ids"].is_array())
            return callback(ApiResponse::error(400, "role_ids 必填且为数组", traceId));
        SystemService::assignRoles(id, body["role_ids"].get<std::vector<int64_t>>(),
                                   okCb(callback, traceId), errCb(callback, traceId));
    }
};

} // namespace hms
