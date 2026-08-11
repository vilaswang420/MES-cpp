#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

#include "controllers/Common.hh"
#include "services/WorkOrderService.hh"

namespace hms {

// 工单接口 (计划任务 14 / 设计文档 4.6)。
// handler 为 Drogon 协程: 返回 Task<HttpResponsePtr> (HttpBinder 仅支持该协程形态,
// 不接受 Task<>+callback 成员函数); Service 抛出的 ApiException 在协程内捕获转信封。
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

    drogon::Task<drogon::HttpResponsePtr> list(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await WorkOrderService::list(
                paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                paramInt(req, "status", -1), paramInt64(req, "line_id", 0), userCtxOf(req));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> detail(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await WorkOrderService::detail(id, userCtxOf(req));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> create(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
        try {
            auto data = co_await WorkOrderService::create(body, userCtxOf(req));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> update(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
        try {
            auto data = co_await WorkOrderService::update(id, body, userCtxOf(req));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> schedule(drogon::HttpRequestPtr req, int64_t id) {
        co_return co_await transitImpl(req, id, "schedule");
    }

    drogon::Task<drogon::HttpResponsePtr> release(drogon::HttpRequestPtr req, int64_t id) {
        co_return co_await transitImpl(req, id, "release");
    }

    drogon::Task<drogon::HttpResponsePtr> start(drogon::HttpRequestPtr req, int64_t id) {
        co_return co_await transitImpl(req, id, "start");
    }

    drogon::Task<drogon::HttpResponsePtr> pause(drogon::HttpRequestPtr req, int64_t id) {
        co_return co_await transitImpl(req, id, "pause");
    }

    drogon::Task<drogon::HttpResponsePtr> complete(drogon::HttpRequestPtr req, int64_t id) {
        co_return co_await transitImpl(req, id, "complete");
    }

    drogon::Task<drogon::HttpResponsePtr> close(drogon::HttpRequestPtr req, int64_t id) {
        co_return co_await transitImpl(req, id, "close");
    }

    drogon::Task<drogon::HttpResponsePtr> report(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
        try {
            auto data = co_await WorkOrderService::report(id, body, userCtxOf(req));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

  private:
    drogon::Task<drogon::HttpResponsePtr> transitImpl(drogon::HttpRequestPtr req, int64_t id,
                                                      const std::string& action) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await WorkOrderService::transit(id, action, userCtxOf(req));
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
            LOG_ERROR << "work order error: " << e.what();
            return ApiResponse::error(500, "服务内部错误", traceId);
        }
    }
};

} // namespace hms
