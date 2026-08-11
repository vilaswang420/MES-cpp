#include <drogon/HttpController.h>

#include "controllers/Common.hh"
#include "services/SystemService.hh"

namespace hms {

// 角色权限管理 (计划任务 11 / 设计文档 4.4 节)
class RoleController : public drogon::HttpController<RoleController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(RoleController::list, "/api/v1/system/roles", drogon::Get);
    ADD_METHOD_TO(RoleController::create, "/api/v1/system/roles", drogon::Post);
    ADD_METHOD_TO(RoleController::get, "/api/v1/system/roles/{id}", drogon::Get);
    ADD_METHOD_TO(RoleController::update, "/api/v1/system/roles/{id}", drogon::Put);
    ADD_METHOD_TO(RoleController::remove, "/api/v1/system/roles/{id}", drogon::Delete);
    ADD_METHOD_TO(RoleController::assignPermissions, "/api/v1/system/roles/{id}/permissions",
                  drogon::Put);
    ADD_METHOD_TO(RoleController::updateDataScope, "/api/v1/system/roles/{id}/data-scope",
                  drogon::Put);
    ADD_METHOD_TO(RoleController::permTree, "/api/v1/system/permissions/tree", drogon::Get);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        SystemService::listRoles(okCb(callback, traceId), errCb(callback, traceId));
    }

    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        SystemService::getRole(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void create(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        SystemService::createRole(body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void update(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        SystemService::updateRole(id, body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void remove(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        SystemService::deleteRole(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void assignPermissions(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("permission_ids") || !body["permission_ids"].is_array())
            return callback(ApiResponse::error(400, "permission_ids 必填且为数组", traceId));
        SystemService::assignPermissions(id, body["permission_ids"].get<std::vector<int64_t>>(),
                                         okCb(callback, traceId), errCb(callback, traceId));
    }

    void updateDataScope(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("data_scope"))
            return callback(ApiResponse::error(400, "data_scope 必填", traceId));
        std::vector<int64_t> deptIds;
        if (body.contains("dept_ids") && body["dept_ids"].is_array())
            deptIds = body["dept_ids"].get<std::vector<int64_t>>();
        SystemService::updateDataScope(id, body["data_scope"].get<int>(), deptIds,
                                       okCb(callback, traceId), errCb(callback, traceId));
    }

    void permTree(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        SystemService::permissionTree(okCb(callback, traceId), errCb(callback, traceId));
    }
};

} // namespace hms
