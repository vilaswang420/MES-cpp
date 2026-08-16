#include <drogon/HttpController.h>

#include "common/ApiResponse.hh"

namespace mes {

// 健康检查 (计划任务 4): 四服务统一 /healthz 探针
class HealthController : public drogon::HttpController<HealthController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::check, "/healthz", drogon::Get);
    METHOD_LIST_END

    void check(const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        callback(ApiResponse::success({{"status", "ok"}, {"service", "mes-backend"}}));
    }
};

} // namespace mes
