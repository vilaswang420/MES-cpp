#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

#include "controllers/Common.hh"
#include "services/ProductionService.hh"

namespace mes {

// 生产主数据与计划 (P1-2.7 主数据 CRUD 补齐)。
// 全部 handler 走协程调 ProductionService; Service 抛 ApiException,
// 由 handleError 转响应信封。路由新增必须同步 perm_routes.cc 与
// 002_seed 种子 (CI 权限映射门禁 scripts/check_perm_mapping.py)。
class ProductionController : public drogon::HttpController<ProductionController> {
  public:
    METHOD_LIST_BEGIN
    // 产线
    ADD_METHOD_TO(ProductionController::lines, "/api/v1/production/lines", drogon::Get);
    ADD_METHOD_TO(ProductionController::createLine, "/api/v1/production/lines", drogon::Post);
    ADD_METHOD_TO(ProductionController::updateLine, "/api/v1/production/lines/{1}", drogon::Put);
    ADD_METHOD_TO(ProductionController::deleteLine, "/api/v1/production/lines/{1}", drogon::Delete);
    ADD_METHOD_TO(ProductionController::stations, "/api/v1/production/lines/{1}/stations",
                  drogon::Get);
    // 工艺路线
    ADD_METHOD_TO(ProductionController::processes, "/api/v1/production/processes", drogon::Get);
    ADD_METHOD_TO(ProductionController::createProcess, "/api/v1/production/processes",
                  drogon::Post);
    ADD_METHOD_TO(ProductionController::updateProcess, "/api/v1/production/processes/{1}",
                  drogon::Put);
    ADD_METHOD_TO(ProductionController::deleteProcess, "/api/v1/production/processes/{1}",
                  drogon::Delete);
    // 产品
    ADD_METHOD_TO(ProductionController::products, "/api/v1/production/products", drogon::Get);
    ADD_METHOD_TO(ProductionController::createProduct, "/api/v1/production/products", drogon::Post);
    ADD_METHOD_TO(ProductionController::updateProduct, "/api/v1/production/products/{1}",
                  drogon::Put);
    ADD_METHOD_TO(ProductionController::deleteProduct, "/api/v1/production/products/{1}",
                  drogon::Delete);
    // 生产计划
    ADD_METHOD_TO(ProductionController::plans, "/api/v1/production/plans", drogon::Get);
    ADD_METHOD_TO(ProductionController::createPlan, "/api/v1/production/plans", drogon::Post);
    METHOD_LIST_END

    // ---- 产线 ----
    drogon::Task<drogon::HttpResponsePtr> lines(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await ProductionService::listLines(
                paramInt(req, "page", 1), paramInt(req, "page_size", 20), paramStr(req, "keyword"));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> createLine(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto body = bodyJson(req);
            if (body.is_null())
                co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
            auto data = co_await ProductionService::createLine(body);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> updateLine(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto body = bodyJson(req);
            if (body.is_null())
                co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
            auto data = co_await ProductionService::updateLine(id, body);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> deleteLine(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await ProductionService::deleteLine(id);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> stations(drogon::HttpRequestPtr req, int64_t lineId) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await ProductionService::listStations(lineId);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    // ---- 工艺路线 ----
    drogon::Task<drogon::HttpResponsePtr> processes(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await ProductionService::listProcesses(
                paramInt(req, "page", 1), paramInt(req, "page_size", 20), paramStr(req, "keyword"));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> createProcess(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto body = bodyJson(req);
            if (body.is_null())
                co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
            auto data = co_await ProductionService::createProcess(body, userCtxOf(req).userId);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> updateProcess(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto body = bodyJson(req);
            if (body.is_null())
                co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
            auto data = co_await ProductionService::updateProcess(id, body);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> deleteProcess(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await ProductionService::deleteProcess(id);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    // ---- 产品 ----
    drogon::Task<drogon::HttpResponsePtr> products(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await ProductionService::listProducts(
                paramInt(req, "page", 1), paramInt(req, "page_size", 20), paramStr(req, "keyword"));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> createProduct(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto body = bodyJson(req);
            if (body.is_null())
                co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
            auto data = co_await ProductionService::createProduct(body);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> updateProduct(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto body = bodyJson(req);
            if (body.is_null())
                co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
            auto data = co_await ProductionService::updateProduct(id, body);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> deleteProduct(drogon::HttpRequestPtr req, int64_t id) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await ProductionService::deleteProduct(id);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    // ---- 生产计划 ----
    drogon::Task<drogon::HttpResponsePtr> plans(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto data = co_await ProductionService::listPlans(paramInt(req, "page", 1),
                                                              paramInt(req, "page_size", 20));
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

    drogon::Task<drogon::HttpResponsePtr> createPlan(drogon::HttpRequestPtr req) {
        auto traceId = traceIdOf(req);
        try {
            auto body = bodyJson(req);
            if (body.is_null())
                co_return ApiResponse::error(400, "请求体必须是 JSON", traceId);
            auto data = co_await ProductionService::createPlan(body, userCtxOf(req).userId);
            co_return ApiResponse::success(data, traceId);
        } catch (...) {
            co_return handleError(std::current_exception(), traceId);
        }
    }

  private:
    static drogon::HttpResponsePtr handleError(const std::exception_ptr& eptr,
                                               const std::string& traceId) {
        try {
            std::rethrow_exception(eptr);
        } catch (const ApiException& e) {
            return ApiResponse::error(e.code(), e.what(), traceId);
        } catch (const std::exception& e) {
            LOG_ERROR << "production error: " << e.what();
            return ApiResponse::error(500, "服务内部错误", traceId);
        }
    }
};

} // namespace mes
