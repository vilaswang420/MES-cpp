#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

#include "controllers/Common.hh"
#include "services/IntegrationService.hh"

namespace mes {

// ERP/WMS 集成接口 (计划任务 23 / 设计文档 4.10 节 7 接口)。
// 协程 handler 风格与 WorkOrderController 一致: ApiException 在协程内捕获转信封。
class IntegrationController : public drogon::HttpController<IntegrationController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(IntegrationController::syncOrders, "/api/v1/integration/erp/sync-orders",
                  drogon::Post);
    ADD_METHOD_TO(IntegrationController::convertOrder, "/api/v1/integration/erp/{1}/convert",
                  drogon::Post);
    ADD_METHOD_TO(IntegrationController::reportErp, "/api/v1/integration/erp/report", drogon::Post);
    ADD_METHOD_TO(IntegrationController::pickRequest, "/api/v1/integration/wms/pick-request",
                  drogon::Post);
    ADD_METHOD_TO(IntegrationController::stockIn, "/api/v1/integration/wms/stock-in", drogon::Post);
    ADD_METHOD_TO(IntegrationController::listLogs, "/api/v1/integration/logs", drogon::Get);
    ADD_METHOD_TO(IntegrationController::retryLog, "/api/v1/integration/logs/{1}/retry",
                  drogon::Post);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> syncOrders(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
        try {
            auto data = co_await IntegrationService::syncErpOrders(body);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> convertOrder(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await IntegrationService::convertErpOrder(id, userCtxOf(req));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> reportErp(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
        auto woId = body.value("work_order_id", (int64_t)0);
        if (woId <= 0)
            co_return ApiResponse::error(400, "work_order_id 必填且大于 0", traceId);
        try {
            auto data = co_await IntegrationService::reportCompletionSaga(woId);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> pickRequest(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
        try {
            auto data = co_await IntegrationService::wmsPickRequest(body);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> stockIn(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
        try {
            auto data = co_await IntegrationService::wmsStockIn(body);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> listLogs(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await IntegrationService::listLogs(
                paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                paramStr(req, "system_type"), paramInt(req, "status", -1));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> retryLog(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await IntegrationService::retryLog(id);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    static drogon::HttpResponsePtr handleError(const std::exception_ptr& eptr,
                                               const std::string& traceId) {
        try {
            std::rethrow_exception(eptr);
        } catch (const ApiException& e) {
            return ApiResponse::error(e.code(), e.what(), traceId);
        } catch (const std::exception& e) {
            LOG_ERROR << "integration error: " << e.what();
            return ApiResponse::error(500, "服务内部错误", traceId);
        }
    }
};

} // namespace mes
