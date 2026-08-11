#include "services/RbacService.hh"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <algorithm>

#include "utils/DataScopeFilter.hh"

namespace hms::RbacService {

namespace {

constexpr int kPermCacheTtlSec = 600; // 10 分钟兜底过期, 主靠主动失效

std::string permKey(int64_t userId) {
    return "perm:user:" + std::to_string(userId);
}

// 回源: 用户 -> 角色 -> 权限码 (仅启用状态)
void loadFromDb(int64_t userId, PermCallback onOk, ErrCallback onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT DISTINCT p.perm_code FROM sys_permissions p "
        "JOIN sys_role_permissions rp ON rp.permission_id = p.id "
        "JOIN sys_roles r ON r.id = rp.role_id "
        "JOIN sys_user_roles ur ON ur.role_id = r.id "
        "WHERE ur.user_id = $1 AND r.status = 1 AND r.deleted = FALSE "
        "AND p.status = 1 AND p.perm_code NOT LIKE 'menu:%'",
        [userId, onOk = std::move(onOk)](const drogon::orm::Result& r) {
            std::set<std::string> perms;
            for (const auto& row : r)
                perms.insert(row["perm_code"].as<std::string>());

            // 回填 Redis (SET + SADD pipeline; 失败不影响本次请求)
            try {
                auto rdb = drogon::app().getRedisClient();
                auto key = permKey(userId);
                rdb->execCommandAsync(
                    [key, perms](const drogon::nosql::RedisResult&) {
                        // 追加成员
                        auto rdb2 = drogon::app().getRedisClient();
                        for (const auto& p : perms) {
                            rdb2->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                                                   [](const drogon::nosql::RedisException&) {},
                                                   "SADD %s %s", key, p);
                        }
                        rdb2->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                                               [](const drogon::nosql::RedisException&) {},
                                               "EXPIRE %s %d", key, kPermCacheTtlSec);
                    },
                    [](const drogon::nosql::RedisException&) {}, "DEL %s", key);
            } catch (...) {
            }
            onOk(perms);
        },
        [onErr = std::move(onErr)](const drogon::orm::DrogonDbException& e) { onErr(e.base()); },
        userId);
}

} // namespace

void getUserPermissionsAsync(int64_t userId, PermCallback onOk, ErrCallback onErr) {
    auto rdb = drogon::app().getRedisClient();
    rdb->execCommandAsync(
        [onOk = std::move(onOk), onErr, userId](const drogon::nosql::RedisResult& result) {
            auto arr = result.asArray();
            if (arr.empty()) {
                // 缓存未命中 (含空权限用户): 回源 DB
                // 注意: 空权限用户也会回源, 频率受 TTL 与登录行为限制, 可接受
                loadFromDb(userId, std::move(onOk), std::move(onErr));
                return;
            }
            std::set<std::string> perms;
            for (const auto& v : arr)
                perms.insert(v.asString());
            onOk(perms);
        },
        [onErr, userId](const drogon::nosql::RedisException&) {
            // Redis 不可用时降级直查 DB, 保证可用性
            loadFromDb(userId, [onErr](const std::set<std::string>&) {}, std::move(onErr));
        },
        "SMEMBERS %s", permKey(userId));
}

void invalidateUserPerm(int64_t userId) {
    try {
        auto rdb = drogon::app().getRedisClient();
        rdb->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                              [](const drogon::nosql::RedisException& e) {
                                  LOG_ERROR << "invalidate perm cache failed: " << e.what();
                              },
                              "DEL %s", permKey(userId));
    } catch (const std::exception& e) {
        LOG_ERROR << "invalidate perm cache failed: " << e.what();
    }
}

void loadMergedScopeAsync(int64_t userId, std::function<void(const ScopeResult&)> onOk,
                          ErrCallback onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT r.role_code, r.data_scope FROM sys_roles r "
        "JOIN sys_user_roles ur ON ur.role_id = r.id "
        "WHERE ur.user_id = $1 AND r.status = 1 AND r.deleted = FALSE",
        [userId, onOk = std::move(onOk), onErr](const drogon::orm::Result& r) {
            ScopeResult res;
            std::vector<int> scopes;
            bool hasCustom = false;
            for (const auto& row : r) {
                res.roleCodes.push_back(row["role_code"].as<std::string>());
                scopes.push_back(row["data_scope"].as<int>());
                if (row["data_scope"].as<int>() == 5)
                    hasCustom = true;
            }
            res.dataScope = DataScopeFilter::mergeDataScope(scopes);
            // 合成结果为 5 时, 合并所有自定义角色的部门集合 (并集)
            if (res.dataScope == 5 && hasCustom) {
                auto db2 = drogon::app().getDbClient();
                db2->execSqlAsync(
                    "SELECT DISTINCT ds.dept_id FROM sys_role_dept_scope ds "
                    "JOIN sys_roles r ON r.id = ds.role_id "
                    "JOIN sys_user_roles ur ON ur.role_id = r.id "
                    "WHERE ur.user_id = $1 AND r.data_scope = 5 AND r.status = 1",
                    [res, onOk](const drogon::orm::Result& r2) mutable {
                        for (const auto& row : r2)
                            res.customDeptIds.push_back(row["dept_id"].as<int64_t>());
                        onOk(res);
                    },
                    [onErr](const drogon::orm::DrogonDbException& e) { onErr(e.base()); }, userId);
                return;
            }
            onOk(res);
        },
        [onErr = std::move(onErr)](const drogon::orm::DrogonDbException& e) { onErr(e.base()); },
        userId);
}

} // namespace hms::RbacService
