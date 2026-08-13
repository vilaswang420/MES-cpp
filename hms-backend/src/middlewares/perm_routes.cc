#include "middlewares/perm_routes.hh"

#include <regex>
#include <vector>

// ⚠ 本文件是 fail-closed 权限映射的"唯一事实源"。
// 新增路由必须同时:
//   1. 在本表注册 (path + method -> perm_code);
//   2. 在 migrations/002_seed (或新迁移) 中补 sys_permissions 记录。
// 权限码与设计文档 4.3-4.6/4.11 节完全一致。
namespace hms::PermRoutes {

namespace {

struct RoutePerm {
    std::regex pattern;
    std::string method;
    std::string perm;
};

std::vector<RoutePerm> g_routes;
std::vector<std::pair<std::regex, std::string>> g_public; // (pattern, method)

// {xxx} 通配段 -> [^/]+
std::regex toRegex(const std::string& pathPattern) {
    std::string out = "^";
    for (size_t i = 0; i < pathPattern.size();) {
        if (pathPattern[i] == '{') {
            auto end = pathPattern.find('}', i);
            out += "[^/]+";
            i = end + 1;
        } else {
            char c = pathPattern[i++];
            if (std::string(".^$|()[]*+?\\").find(c) != std::string::npos)
                out += '\\';
            out += c;
        }
    }
    out += "$";
    return std::regex(out);
}

void add(const std::string& path, const std::string& method, const std::string& perm) {
    g_routes.push_back({toRegex(path), method, perm});
}

void addPublic(const std::string& path, const std::string& method) {
    g_public.emplace_back(toRegex(path), method);
}

} // namespace

void init() {
    g_routes.clear();
    g_public.clear();

    // ---- 公开白名单 (显式放行, 不允许"恰好没配权限"式放行) ----
    addPublic("/healthz", "GET");
    // Prometheus 抓取端点 (任务 28): 只读运行指标, 生产由 Nginx 限制来源网段
    addPublic("/metrics", "GET");
    addPublic("/api/v1/auth/login", "POST");
    addPublic("/api/v1/auth/captcha", "GET");
    // WS 升级请求放行 advice 链; 身份在 WsController 以 query token 严格校验 (fail-closed)
    addPublic("/ws", "GET");
    addPublic("/ws/dashboard", "GET");

    // ---- 认证 (Bearer) ----
    add("/api/v1/auth/refresh", "POST", "auth:bearer");
    add("/api/v1/auth/logout", "POST", "auth:bearer");
    add("/api/v1/auth/profile", "GET", "auth:bearer");
    add("/api/v1/auth/password", "PUT", "auth:bearer");

    // ---- 用户管理 (4.3) ----
    add("/api/v1/system/users", "GET", "system:user:list");
    add("/api/v1/system/users/{id}", "GET", "system:user:query");
    add("/api/v1/system/users", "POST", "system:user:add");
    add("/api/v1/system/users/{id}", "PUT", "system:user:update");
    add("/api/v1/system/users/{id}", "DELETE", "system:user:delete");
    add("/api/v1/system/users/{id}/reset-password", "PUT", "system:user:reset");
    add("/api/v1/system/users/{id}/status", "PUT", "system:user:update");
    add("/api/v1/system/users/{id}/roles", "PUT", "system:user:assign");

    // ---- 角色权限 (4.4) ----
    add("/api/v1/system/roles", "GET", "system:role:list");
    add("/api/v1/system/roles/{id}", "GET", "system:role:query");
    add("/api/v1/system/roles", "POST", "system:role:add");
    add("/api/v1/system/roles/{id}", "PUT", "system:role:update");
    add("/api/v1/system/roles/{id}", "DELETE", "system:role:delete");
    add("/api/v1/system/roles/{id}/permissions", "PUT", "system:role:assign");
    add("/api/v1/system/roles/{id}/data-scope", "PUT", "system:role:update");
    add("/api/v1/system/permissions/tree", "GET", "system:permission:list");

    // ---- 部门 (4.5) ----
    add("/api/v1/system/departments/tree", "GET", "system:dept:list");
    add("/api/v1/system/departments", "POST", "system:dept:add");
    add("/api/v1/system/departments/{id}", "PUT", "system:dept:update");
    add("/api/v1/system/departments/{id}", "DELETE", "system:dept:delete");

    // ---- 审计与配置 (4.11) ----
    add("/api/v1/system/audit-logs", "GET", "system:audit:list");
    add("/api/v1/system/audit-logs/{id}", "GET", "system:audit:list");
    add("/api/v1/system/configs", "GET", "system:config:list");
    add("/api/v1/system/configs/{key}", "PUT", "system:config:update");

    // ---- 工单 (4.6) ----
    add("/api/v1/production/work-orders", "GET", "prod:wo:list");
    add("/api/v1/production/work-orders/{id}", "GET", "prod:wo:query");
    add("/api/v1/production/work-orders", "POST", "prod:wo:add");
    add("/api/v1/production/work-orders/{id}", "PUT", "prod:wo:update");
    add("/api/v1/production/work-orders/{id}/schedule", "PUT", "prod:wo:schedule");
    add("/api/v1/production/work-orders/{id}/release", "PUT", "prod:wo:release");
    add("/api/v1/production/work-orders/{id}/start", "PUT", "prod:wo:start");
    add("/api/v1/production/work-orders/{id}/pause", "PUT", "prod:wo:pause");
    add("/api/v1/production/work-orders/{id}/complete", "PUT", "prod:wo:complete");
    add("/api/v1/production/work-orders/{id}/close", "PUT", "prod:wo:close");
    add("/api/v1/production/work-orders/{id}/report", "POST", "prod:wo:report");

    // ---- 产线/工位/工艺/产品/计划 (4.6) ----
    add("/api/v1/production/lines", "GET", "prod:line:list");
    add("/api/v1/production/lines", "POST", "prod:line:add");
    add("/api/v1/production/lines/{id}/stations", "GET", "prod:station:list");
    add("/api/v1/production/processes", "GET", "prod:process:list");
    add("/api/v1/production/processes", "POST", "prod:process:add");
    add("/api/v1/production/products", "GET", "prod:product:list");
    add("/api/v1/production/products", "POST", "prod:product:add");
    add("/api/v1/production/plans", "GET", "prod:plan:list");
    add("/api/v1/production/plans", "POST", "prod:plan:add");

    // ---- IoT 设备域 (4.7) ----
    add("/api/v1/iot/devices", "GET", "iot:device:list");
    add("/api/v1/iot/devices/{id}", "GET", "iot:device:query");
    add("/api/v1/iot/devices", "POST", "iot:device:add");
    add("/api/v1/iot/devices/{id}", "PUT", "iot:device:update");
    add("/api/v1/iot/devices/{id}", "DELETE", "iot:device:delete");
    add("/api/v1/iot/devices/{id}/status", "GET", "iot:device:query");
    add("/api/v1/iot/devices/{id}/sensors", "GET", "iot:sensor:list");
    add("/api/v1/iot/devices/{id}/sensors", "POST", "iot:sensor:add");
    add("/api/v1/iot/devices/{id}/realtime-data", "GET", "iot:data:query");
    add("/api/v1/iot/sensors/{id}/history", "GET", "iot:data:query");
    add("/api/v1/iot/alerts", "GET", "iot:alert:list");
    add("/api/v1/iot/alerts/{id}/acknowledge", "PUT", "iot:alert:handle");
    add("/api/v1/iot/devices/{id}/command", "POST", "iot:device:command");
    add("/api/v1/iot/tasks", "GET", "iot:task:list");
    add("/api/v1/iot/tasks", "POST", "iot:task:add");
    add("/api/v1/iot/tasks/{id}", "PUT", "iot:task:update");
    add("/api/v1/iot/tasks/{id}", "DELETE", "iot:task:delete");
    add("/api/v1/iot/tasks/{id}/toggle", "PUT", "iot:task:update");

    // ---- 质量域 (4.8) ----
    add("/api/v1/quality/standards", "GET", "qc:standard:list");
    add("/api/v1/quality/inspections", "POST", "qc:inspection:add");
    add("/api/v1/quality/inspections", "GET", "qc:inspection:list");
    add("/api/v1/quality/inspections/{id}", "GET", "qc:inspection:query");
    add("/api/v1/quality/defects", "GET", "qc:defect:list");
    add("/api/v1/quality/defects/{id}/disposition", "PUT", "qc:defect:handle");
    add("/api/v1/quality/statistics", "GET", "qc:stat:view");

    // ---- ERP/WMS 集成 (4.10) ----
    add("/api/v1/integration/erp/sync-orders", "POST", "integ:erp:sync");
    add("/api/v1/integration/erp/{id}/convert", "POST", "integ:erp:convert");
    add("/api/v1/integration/erp/report", "POST", "integ:erp:report");
    add("/api/v1/integration/wms/pick-request", "POST", "integ:wms:pick");
    add("/api/v1/integration/wms/stock-in", "POST", "integ:wms:inbound");
    add("/api/v1/integration/logs", "GET", "integ:log:list");
    add("/api/v1/integration/logs/{id}/retry", "POST", "integ:log:retry");
}

std::string getPermission(const std::string& path, const std::string& method) {
    for (const auto& r : g_routes) {
        if (r.method == method && std::regex_match(path, r.pattern))
            return r.perm;
    }
    return ""; // 未注册 -> fail-closed
}

bool isPublicPath(const std::string& path, const std::string& method) {
    for (const auto& [pattern, m] : g_public) {
        if (m == method && std::regex_match(path, pattern))
            return true;
    }
    return false;
}

} // namespace hms::PermRoutes
