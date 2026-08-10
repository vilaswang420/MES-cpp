#pragma once

#include <drogon/HttpMiddleware.h>

#include <random>
#include <sstream>

// TraceMiddleware: 为每个请求生成 trace_id, 注入请求属性并回写响应头 X-Trace-Id。
// JSON 行日志由各层统一携带 trace_id (Drogon MDC: drogon::trantor 日志上下文)。
namespace hms {

inline std::string genTraceId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << rng() << rng();
    return oss.str().substr(0, 24);
}

class TraceMiddleware : public drogon::HttpMiddleware<TraceMiddleware> {
  public:
    void invoke(const drogon::HttpRequestPtr& req, MiddlewareNextCallback&& nextCb,
                MiddlewareCallback&& mcb) override {
        auto traceId = req->getHeader("X-Trace-Id");
        if (traceId.empty())
            traceId = genTraceId();
        req->getAttributes()->insert("trace_id", traceId);

        // 包装回调: 在响应上回写 X-Trace-Id, 便于前端上报与排障
        auto traceCopy = traceId;
        nextCb(req, [mcb = std::move(mcb), traceCopy](const drogon::HttpResponsePtr& resp) {
            if (resp)
                resp->addHeader("X-Trace-Id", traceCopy);
            mcb(resp);
        });
    }
};

} // namespace hms
