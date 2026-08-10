#pragma once

#include <drogon/HttpMiddleware.h>

// RBAC 权限检查中间件 (设计文档 5.3 节), fail-closed:
// 公开接口显式白名单放行; 未注册权限映射的路由一律 403。
namespace hms {

class RbacMiddleware : public drogon::HttpMiddleware<RbacMiddleware> {
  public:
    void invoke(const drogon::HttpRequestPtr& req, MiddlewareNextCallback&& nextCb,
                MiddlewareCallback&& mcb) override;
};

} // namespace hms
