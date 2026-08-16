#include <drogon/HttpController.h>

#include "utils/Metrics.hh"

namespace mes {

// Prometheus 抓取端点 (计划任务 28): 公开白名单放行 (perm_routes),
// 仅暴露运行指标, 不含业务数据; 生产由 Nginx 限制来源网段。
class MetricsController : public drogon::HttpController<MetricsController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MetricsController::render, "/metrics", drogon::Get);
    METHOD_LIST_END

    void render(const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeCodeAndCustomString(drogon::CT_CUSTOM,
                                                "text/plain; version=0.0.4; charset=utf-8");
        resp->setBody(Metrics::render());
        callback(resp);
    }
};

} // namespace mes
