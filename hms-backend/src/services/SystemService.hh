#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// 系统管理 CRUD (计划任务 11 / 设计文档 4.3-4.5, 4.11 节):
// 用户 / 角色 / 部门 / 权限树 / 数据范围 / 审计查询 / 系统配置。
// 写操作完成后主动失效相关用户的权限缓存 (perm:user:{userId})。
namespace hms::SystemService {

using JsonCb = std::function<void(const nlohmann::json&)>;
using ErrCb = std::function<void(int code, const std::string& msg)>;

// ---- 用户 (4.3) ----
void listUsers(int page, int pageSize, const std::string& keyword, int status, JsonCb onOk,
               ErrCb onErr);
void getUser(int64_t id, JsonCb onOk, ErrCb onErr);
void createUser(const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void updateUser(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void deleteUser(int64_t id, JsonCb onOk, ErrCb onErr);
void resetPassword(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void setUserStatus(int64_t id, int status, JsonCb onOk, ErrCb onErr);
void assignRoles(int64_t userId, const std::vector<int64_t>& roleIds, JsonCb onOk, ErrCb onErr);

// ---- 角色 (4.4) ----
void listRoles(JsonCb onOk, ErrCb onErr);
void getRole(int64_t id, JsonCb onOk, ErrCb onErr);
void createRole(const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void updateRole(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void deleteRole(int64_t id, JsonCb onOk, ErrCb onErr);
void assignPermissions(int64_t roleId, const std::vector<int64_t>& permIds, JsonCb onOk,
                       ErrCb onErr);
void updateDataScope(int64_t roleId, int dataScope, const std::vector<int64_t>& deptIds,
                     JsonCb onOk, ErrCb onErr);

// ---- 权限树 / 部门 (4.3-4.5) ----
void permissionTree(JsonCb onOk, ErrCb onErr);
void deptTree(JsonCb onOk, ErrCb onErr);
void createDept(const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void updateDept(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void deleteDept(int64_t id, JsonCb onOk, ErrCb onErr);

// ---- 审计与配置 (4.11) ----
// 审计日志过滤条件 (全部可选; responseCode < 0 表示不过滤)
struct AuditLogFilter {
    int64_t userId = 0;    // >0 时按用户过滤
    std::string module;    // 非空时按模块精确过滤
    std::string operation; // 非空时按操作模糊过滤 (ILIKE)
    int responseCode = -1; // >=0 时按响应码精确过滤
    std::string ip;        // 非空时按 IP 模糊过滤 (ILIKE)
    std::string startTime; // 非空时 created_at >= startTime (ISO 8601, 触发按月分区裁剪)
    std::string endTime;   // 非空时 created_at <= endTime
};
void listAuditLogs(int page, int pageSize, const AuditLogFilter& filter, JsonCb onOk, ErrCb onErr);
void getAuditLog(int64_t id, JsonCb onOk, ErrCb onErr);
void listConfigs(JsonCb onOk, ErrCb onErr);
void updateConfig(const std::string& key, const std::string& value, JsonCb onOk, ErrCb onErr);

} // namespace hms::SystemService
