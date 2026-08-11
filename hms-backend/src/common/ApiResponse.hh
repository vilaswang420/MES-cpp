#pragma once

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

#include "utils/TimeUtils.hh"

// 统一响应信封 (设计文档 4.1 节):
// {code, message, data, timestamp, trace_id}
// 时间一律 UTC ISO8601 带 Z; 业务代码禁止手拼错误 JSON, 只允许抛 ApiException,
// 错误响应由 main.cc 中 registerHandlingErrorAdvice 的全局拦截器统一产出。
namespace hms {

class ApiException : public std::runtime_error {
  public:
    ApiException(int code, std::string message) : std::runtime_error(message), code_(code) {}
    int code() const { return code_; }

  private:
    int code_;
};

// 常用语义化构造
inline ApiException BadRequest(const std::string& msg) {
    return ApiException(400, msg);
}
inline ApiException Unauthorized(const std::string& msg) {
    return ApiException(401, msg);
}
inline ApiException Forbidden(const std::string& msg) {
    return ApiException(403, msg);
}
inline ApiException NotFound(const std::string& msg) {
    return ApiException(404, msg);
}
inline ApiException Conflict(const std::string& msg) {
    return ApiException(409, msg);
}
inline ApiException TooManyRequests(const std::string& msg) {
    return ApiException(429, msg);
}
inline ApiException Internal(const std::string& msg) {
    return ApiException(500, msg);
}

class ApiResponse {
  public:
    // 成功响应; traceId 由 TraceMiddleware 注入请求属性后透传
    static drogon::HttpResponsePtr success(const nlohmann::json& data,
                                           const std::string& traceId = "") {
        return build(200, "success", &data, traceId);
    }

    // 错误响应: 仅允许全局错误拦截器调用 (见 main.cc)
    static drogon::HttpResponsePtr error(int code, const std::string& message,
                                         const std::string& traceId = "") {
        return build(code, message, nullptr, traceId);
    }

  private:
    static drogon::HttpResponsePtr build(int code, const std::string& message,
                                         const nlohmann::json* data, const std::string& traceId) {
        nlohmann::json body;
        body["code"] = code;
        body["message"] = message;
        if (data != nullptr)
            body["data"] = *data;
        body["timestamp"] = TimeUtils::nowUtcIso();
        body["trace_id"] = traceId;

        // Drogon 的 newHttpJsonResponse 收 jsoncpp 的 Json::Value; 本项目用 nlohmann,
        // 故手工序列化后以字符串体构造, 并显式声明 JSON 内容类型
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody(body.dump());
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        resp->setContentTypeString("application/json; charset=utf-8");
        if (code != 200) {
            // HTTP 状态码与业务 code 对齐, 便于网关/前端拦截
            resp->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
        }
        if (!traceId.empty())
            resp->addHeader("X-Trace-Id", traceId);
        return resp;
    }
};

// 从请求属性中取 trace_id (TraceMiddleware 写入)
inline std::string traceIdOf(const drogon::HttpRequestPtr& req) {
    if (req && req->getAttributes() && req->getAttributes()->find("trace_id"))
        return req->getAttributes()->get<std::string>("trace_id");
    return "";
}

} // namespace hms
