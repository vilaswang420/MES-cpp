#include <drogon/HttpController.h>
#include <drogon/utils/Coroutine.h>

#include "controllers/Common.hh"
#include "services/WorkOrderService.hh"

namespace hms {

// 工单接口 (计划任务 14 / 设计文档 4.6 节)。
// handler 为 Drogon 协程: Service 抛出的 ApiException 在此统一转响应信封。
class WorkOrderController : public drogon::HttpController<WorkOrderController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WorkOrderController::list, "/api/v1/production/work-orders", drogon::Get);
    ADD_METHOD_TO(WorkOrderController::create, "/api/v1/production/work-orders", drogon::Post);
    ADD_METHOD_TO(WorkOrderController::detail, "/api/v1/production/work-orders/{1}", drogon::Get);
    ADD_METHOD_TO(WorkOrderController::update, "/api/v1/production/work-orders/{1}", drogon::Put);
    ADD_METHOD_TO(WorkOrderController::schedule, "/api/v1/production/work-orders/{1}/schedule",
                  drogon::Put);
    ADD_METHOD_TO(WorkOrderController::release, "/api/v1/production/work-orders/{1}/release",
                  drogon::Put);
    ADD_METHOD_TO(WorkOrderController::start, "/api/v1/production/work-orders/{1}/start",
                  drogon::Put);
    ADD_METHOD_TO(WorkOrderController::pause, "/api/v1/production/work-orders/{1}/pause",
                  drogon::Put);
    ADD_METHOD_TO(WorkOrderController::complete, "/api/v1/production/work-orders/{1}/complete",
                  drogon::Put);
    ADD_METHOD_TO(WorkOrderController::close, "/api/v1/production/work-orders/{1}/close",
                  drogon::Put);
    ADD_METHOD_TO(WorkOrderController::report, "/api/v1/production/work-orders/{1}/report",
                  drogon::Post);
    METHOD_LIST_END

    drogon::Task<> list(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await WorkOrderService::list(
                paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                paramInt(req, "status", -1), paramInt64(req, "line_id", 0), userCtxOf(req));
            callback(ApiResponse::success(data, traceId));
        } catch (...) {
            handleError(std::current_exception(), callback, traceId);
        }
    }

    drogon::Task<> detail(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await WorkOrderService::detail(id, userCtxOf(req));
            callback(ApiResponse::success(data, traceId));
        } catch (...) {
            handleError(std::current_exception(), callback, traceId);
        }
    }

    drogon::Task<> create(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = req->getJsonObject();
        if (!body) {
            callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
            co_return;
        }
        try {
            auto data = co_await WorkOrderService::create(*body, userCtxOf(req));
            callback(ApiResponse::success(data, traceId));
        } catch (...) {
            handleError(std::current_exception(), callback, traceId);
        }
    }

    drogon::Task<> update(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = req->getJsonObject();
        if (!body) {
            callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
            co_return;
        }
        try {
            auto data = co_await WorkOrderService::update(id, *body, userCtxOf(req));
            callback(ApiResponse::success(data, traceId));
        } catch (...) {
            handleError(std::current_exception(), callback, traceId);
        }
    }

    drogon::Task<> schedule(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            int64_t id) {
        co_await transitImpl(req, std::move(callback), id, "schedule");
    }

    drogon::Task<> release(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           int64_t id) {
        co_await transitImpl(req, std::move(callback), id, "release");
    }

    drogon::Task<> start(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         int64_t id) {
        co_await transitImpl(req, std::move(callback), id, "start");
    }

    drogon::Task<> pause(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         int64_t id) {
        co_await transitImpl(req, std::move(callback), id, "pause");
    }

    drogon::Task<> complete(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            int64_t id) {
        co_await transitImpl(req, std::move(callback), id, "complete");
    }

    drogon::Task<> close(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         int64_t id) {
        co_await transitImpl(req, std::move(callback), id, "close");
    }

    drogon::Task<> report(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = req->getJsonObject();
        if (!body) {
            callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
            co_return;
        }
        try {
            auto data = co_await WorkOrderService::report(id, *body, userCtxOf(req));
            callback(ApiResponse::success(data, traceId));
        } catch (...) {
            handleError(std::current_exception(), callback, traceId);
        }
    }

  private:
    drogon::Task<> transitImpl(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               int64_t id, const std::string& action) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await WorkOrderService::transit(id, action, userCtxOf(req));
            callback(ApiResponse::success(data, traceId));
        } catch (...) {
            handleError(std::current_exception(), callback, traceId);
        }
    }

    static void handleError(const std::exception_ptr& eptr,
                            const std::function<void(const drogon::HttpResponsePtr&)>& callback,
                            const std::string& traceId) {
        try {
            std::rethrow_exception(eptr);
        } catch (const ApiException& e) {
            callback(ApiResponse::error(e.code(), e.what(), traceId));
        } catch (const std::exception& e) {
            LOG_ERROR << "work order error: " << e.what();
            callback(ApiResponse::error(500, "服务内部错误", traceId));
        }
    }
};

} // namespace hms
