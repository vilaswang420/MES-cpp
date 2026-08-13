#include <drogon/HttpController.h>

#include "controllers/Common.hh"
#include "services/QcService.hh"

namespace hms {

// 质量域 (计划任务 21 / 设计文档 4.8 节 7 接口)
class QualityController : public drogon::HttpController<QualityController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(QualityController::listStandards, "/api/v1/quality/standards", drogon::Get);
    ADD_METHOD_TO(QualityController::createInspection, "/api/v1/quality/inspections", drogon::Post);
    ADD_METHOD_TO(QualityController::listInspections, "/api/v1/quality/inspections", drogon::Get);
    ADD_METHOD_TO(QualityController::getInspection, "/api/v1/quality/inspections/{1}", drogon::Get);
    ADD_METHOD_TO(QualityController::listDefects, "/api/v1/quality/defects", drogon::Get);
    ADD_METHOD_TO(QualityController::handleDefect, "/api/v1/quality/defects/{1}/disposition",
                  drogon::Put);
    ADD_METHOD_TO(QualityController::statistics, "/api/v1/quality/statistics", drogon::Get);
    METHOD_LIST_END

    void listStandards(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        QcService::listStandards(paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                                 paramStr(req, "keyword"), paramInt64(req, "product_id", 0),
                                 okCb(callback, traceId), errCb(callback, traceId));
    }

    void createInspection(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        QcService::createInspection(body, userCtxOf(req).userId, okCb(callback, traceId),
                                    errCb(callback, traceId));
    }

    void listInspections(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        QcService::listInspections(paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                                   paramInt64(req, "work_order_id", 0),
                                   paramInt(req, "result", -1), okCb(callback, traceId),
                                   errCb(callback, traceId));
    }

    void getInspection(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       int64_t id) {
        auto traceId = traceIdOf(req);
        QcService::getInspection(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void listDefects(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        QcService::listDefects(paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                               paramInt64(req, "work_order_id", 0),
                               paramInt(req, "disposition", -1), paramStr(req, "category"),
                               okCb(callback, traceId), errCb(callback, traceId));
    }

    void handleDefect(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        QcService::handleDefect(id, body, userCtxOf(req).userId, notNullCb(callback, traceId),
                                errCb(callback, traceId));
    }

    void statistics(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        QcService::statistics(paramStr(req, "start_date"), paramStr(req, "end_date"),
                              okCb(callback, traceId), errCb(callback, traceId));
    }
};

} // namespace hms
