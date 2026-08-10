#include "middlewares/RbacMiddleware.hh"

#include <drogon/drogon.h>

#include "common/ApiResponse.hh"
#include "middlewares/perm_routes.hh"
#include "services/RbacService.hh"

namespace hms {

void RbacMiddleware::invoke(const drogon::HttpRequestPtr& req, MiddlewareNextCallback&& nextCb,
                            MiddlewareCallback&& mcb) {
    const auto path = req->path();
    const auto method = req->methodString();
    const auto traceId = traceIdOf(req);

    // 1. 公开接口通过显式白名单放行 (与 JwtMiddleware 的 isPublicPath 一致)
    if (PermRoutes::isPublicPath(path, method)) {
        nextCb(req, std::move(mcb));
        return;
    }

    // 2. 解析当前路由所需权限
    auto requiredPerm = PermRoutes::getPermission(path, method);

    // 3. fail-closed: 未注册权限映射的路由一律拒绝,
    //    防止新增/遗漏配置的接口对所有登录用户开放
    if (requiredPerm.empty()) {
        LOG_WARN << "fail-closed: route without perm mapping: " << method << " " << path;
        mcb(ApiResponse::error(403, "路由未配置权限映射", traceId));
        return;
    }

    // 4. "auth:bearer" 表示仅需登录即可 (认证类自身接口), JwtMiddleware 已验证身份
    if (requiredPerm == "auth:bearer") {
        nextCb(req, std::move(mcb));
        return;
    }

    // 5. 获取用户权限集 (Redis 缓存 perm:user:{userId}, 主动失效)
    auto attrs = req->getAttributes();
    auto userId = attrs->get<int64_t>("current_user_id");
    RbacService::getUserPermissionsAsync(
        userId,
        [req, requiredPerm, nextCb = std::move(nextCb), mcb = std::move(mcb),
         traceId](const std::set<std::string>& perms) mutable {
            if (perms.find(requiredPerm) == perms.end()) {
                mcb(ApiResponse::error(403, "无权限: " + requiredPerm, traceId));
                return;
            }
            // 6. 记录本次路由所需权限, 供 AuditMiddleware 使用
            req->getAttributes()->insert("required_perm", requiredPerm);
            nextCb(req, std::move(mcb));
        },
        [mcb, traceId](const std::exception& e) {
            // 权限查询失败 fail-closed
            LOG_ERROR << "load user permissions failed: " << e.what();
            mcb(ApiResponse::error(500, "权限服务暂不可用", traceId));
        });
}

} // namespace hms
