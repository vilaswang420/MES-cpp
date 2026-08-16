#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <cstdlib>
#include <functional>
#include <string>

#include "common/ApiResponse.hh"
#include "services/WorkOrderService.hh"

// Controller 层公共辅助: 从请求属性提取 JWT 上下文与查询参数
namespace mes {

// Drogon 的 getJsonObject 返回 jsoncpp 的 Json::Value, 而全项统一用 nlohmann::json,
// 故直接解析请求原始体; 解析失败/空体返回 null json (调用方用 is_null() 判定)
inline nlohmann::json bodyJson(const drogon::HttpRequestPtr& req) {
    try {
        return nlohmann::json::parse(req->getBody());
    } catch (...) {
        return nlohmann::json();
    }
}

inline WorkOrderService::UserCtx userCtxOf(const drogon::HttpRequestPtr& req) {
    WorkOrderService::UserCtx ctx;
    auto attrs = req->getAttributes();
    if (attrs->find("current_user_id"))
        ctx.userId = attrs->get<int64_t>("current_user_id");
    if (attrs->find("current_username"))
        ctx.username = attrs->get<std::string>("current_username");
    if (attrs->find("current_dept_id"))
        ctx.deptId = attrs->get<int64_t>("current_dept_id");
    if (attrs->find("data_scope"))
        ctx.dataScope = attrs->get<int>("data_scope");
    if (attrs->find("custom_dept_ids"))
        ctx.customDeptIds = attrs->get<std::vector<int64_t>>("custom_dept_ids");
    return ctx;
}

inline int paramInt(const drogon::HttpRequestPtr& req, const std::string& key, int def) {
    auto v = req->getParameter(key);
    if (v.empty())
        return def;
    return std::atoi(v.c_str());
}

inline int64_t paramInt64(const drogon::HttpRequestPtr& req, const std::string& key, int64_t def) {
    auto v = req->getParameter(key);
    if (v.empty())
        return def;
    return std::atoll(v.c_str());
}

inline std::string paramStr(const drogon::HttpRequestPtr& req, const std::string& key) {
    return req->getParameter(key);
}

using HttpCb = std::function<void(const drogon::HttpResponsePtr&)>;
using JsonFn = std::function<void(const nlohmann::json&)>;
using ErrFn = std::function<void(int, const std::string&)>;

// 成功回调
inline JsonFn okCb(const HttpCb& cb, const std::string& traceId) {
    return [cb, traceId](const nlohmann::json& data) { cb(ApiResponse::success(data, traceId)); };
}

// Service 以 onOk(nullptr) 表示资源不存在, {"conflict":true} 表示删除冲突
inline JsonFn notNullCb(const HttpCb& cb, const std::string& traceId) {
    return [cb, traceId](const nlohmann::json& data) {
        if (data.is_null())
            cb(ApiResponse::error(404, "资源不存在", traceId));
        else if (data.contains("conflict"))
            cb(ApiResponse::error(409, "存在子节点或关联数据, 禁止删除", traceId));
        else
            cb(ApiResponse::success(data, traceId));
    };
}

inline ErrFn errCb(const HttpCb& cb, const std::string& traceId) {
    return [cb, traceId](int code, const std::string& msg) {
        cb(ApiResponse::error(code, msg, traceId));
    };
}

} // namespace mes
