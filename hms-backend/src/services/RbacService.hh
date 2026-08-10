#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

// RBAC 权限服务 (计划任务 11):
// - getUserPermissionsAsync: Redis 缓存 perm:user:{userId} 主动失效
// - invalidateUserPerm: 角色/用户变更后主动清缓存
// - login 时计算多角色合成 data_scope (取最宽, 5.4 节)
namespace hms::RbacService {

using PermCallback = std::function<void(const std::set<std::string>&)>;
using ErrCallback = std::function<void(const std::exception&)>;

// 读用户权限码集合: 先 Redis (SISMEMBER 全量 SMEMBERS), 未命中回源 DB 并回填
void getUserPermissionsAsync(int64_t userId, PermCallback onOk, ErrCallback onErr);

// 主动失效缓存 (分配角色/权限/删除用户后调用)
void invalidateUserPerm(int64_t userId);

// 登录时加载角色码列表与各角色 data_scope, 合成最宽 data_scope 与自定义部门并集
struct ScopeResult {
    int dataScope = 1;
    std::vector<int64_t> customDeptIds;
    std::vector<std::string> roleCodes;
};
void loadMergedScopeAsync(int64_t userId, std::function<void(const ScopeResult&)> onOk,
                          ErrCallback onErr);

} // namespace hms::RbacService
