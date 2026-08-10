#pragma once

#include <drogon/HttpMiddleware.h>

#include "common/ApiResponse.hh"
#include "middlewares/perm_routes.hh"
#include "utils/JwtUtils.hh"

// JWT 认证中间件 (设计文档 5.3 节):
// 白名单跳过 -> 提取 Bearer -> 验签+过期 -> Redis 黑名单 -> 注入用户上下文。
namespace hms {

class JwtMiddleware : public drogon::HttpMiddleware<JwtMiddleware> {
  public:
    void invoke(const drogon::HttpRequestPtr& req, MiddlewareNextCallback&& nextCb,
                MiddlewareCallback&& mcb) override;
};

} // namespace hms
