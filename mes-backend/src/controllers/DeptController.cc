#include <drogon/HttpController.h>

#include "controllers/Common.hh"
#include "services/SystemService.hh"

namespace mes {

// 部门管理 (计划任务 11 / 设计文档 4.5 节)
class DeptController : public drogon::HttpController<DeptController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DeptController::tree, "/api/v1/system/departments/tree", drogon::Get);
    ADD_METHOD_TO(DeptController::create, "/api/v1/system/departments", drogon::Post);
    ADD_METHOD_TO(DeptController::update, "/api/v1/system/departments/{id}", drogon::Put);
    ADD_METHOD_TO(DeptController::remove, "/api/v1/system/departments/{id}", drogon::Delete);
    METHOD_LIST_END

    void tree(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        SystemService::deptTree(okCb(callback, traceId), errCb(callback, traceId));
    }

    void create(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        SystemService::createDept(body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void update(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        SystemService::updateDept(id, body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void remove(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        SystemService::deleteDept(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }
};

// 审计日志查询 (设计文档 4.11 节)
class AuditLogController : public drogon::HttpController<AuditLogController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuditLogController::list, "/api/v1/system/audit-logs", drogon::Get);
    ADD_METHOD_TO(AuditLogController::get, "/api/v1/system/audit-logs/{id}", drogon::Get);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        SystemService::AuditLogFilter f;
        f.userId = paramInt64(req, "user_id", 0);
        f.module = paramStr(req, "module");
        f.operation = paramStr(req, "operation");
        f.responseCode = paramInt(req, "response_code", -1);
        f.ip = paramStr(req, "ip");
        f.startTime = paramStr(req, "start_time");
        f.endTime = paramStr(req, "end_time");
        SystemService::listAuditLogs(paramInt(req, "page", 1), paramInt(req, "page_size", 20), f,
                                     okCb(callback, traceId), errCb(callback, traceId));
    }

    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        SystemService::getAuditLog(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }
};

// 系统配置 (设计文档 4.11 节)
class ConfigController : public drogon::HttpController<ConfigController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigController::list, "/api/v1/system/configs", drogon::Get);
    ADD_METHOD_TO(ConfigController::update, "/api/v1/system/configs/{key}", drogon::Put);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        SystemService::listConfigs(okCb(callback, traceId), errCb(callback, traceId));
    }

    void update(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& key) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("config_value"))
            return callback(ApiResponse::error(400, "config_value 必填", traceId));
        SystemService::updateConfig(key, body["config_value"].get<std::string>(),
                                    notNullCb(callback, traceId), errCb(callback, traceId));
    }
};

} // namespace mes
