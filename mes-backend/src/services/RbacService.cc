#include "services/RbacService.hh"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "common/SqlParam.hh"
#include "utils/DataScopeFilter.hh"

namespace mes::RbacService {

namespace {

constexpr int kPermCacheTtlSec = 600; // 10 分钟兜底过期, 主靠主动失效
// 空权限哨兵: 无角色用户的权限集为空, Redis set 无法区分"空"与"未缓存",
// 写入哨兵成员避免每次请求都回源 DB (压测高频登录场景)
constexpr const char* kEmptyPermSentinel = "__none__";

// data_scope 合成结果内存缓存 (登录路径每次调 loadMergedScopeAsync):
// 短 TTL + 角色变更主动失效, 避免高频登录重复 join 查询
struct ScopeCacheEntry {
    ScopeResult scope;
    std::chrono::steady_clock::time_point expireAt;
};
std::mutex g_scopeMtx;
std::unordered_map<int64_t, ScopeCacheEntry> g_scopeCache;
constexpr int kScopeCacheTtlSec = 60;

std::string permKey(int64_t userId) {
    return "perm:user:" + std::to_string(userId);
}

// 回源单飞: 同一用户的并发缓存未命中 (如压测/重登场景) 合并为一次 DB 查询,
// 避免 N 个并发登录击穿缓存引发 N 次重复 join 查询压垮 DB
std::mutex g_flightMtx;
std::unordered_map<int64_t, std::vector<std::pair<PermCallback, ErrCallback>>>* g_inflight =
    nullptr;

void forwardResult(int64_t userId, const std::set<std::string>* perms, const std::exception* err) {
    std::vector<std::pair<PermCallback, ErrCallback>> waiters;
    {
        std::lock_guard lk(g_flightMtx);
        if (!g_inflight)
            return;
        auto it = g_inflight->find(userId);
        if (it == g_inflight->end())
            return;
        waiters = std::move(it->second);
        g_inflight->erase(it);
    }
    for (auto& [ok, fail] : waiters) {
        if (perms)
            ok(*perms);
        else
            fail(*err);
    }
}

// 回源: 用户 -> 角色 -> 权限码 (仅启用状态); 由 single-flight 调度, 同一用户并发仅执行一次
void doLoadFromDb(int64_t userId) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT DISTINCT p.perm_code FROM sys_permissions p "
        "JOIN sys_role_permissions rp ON rp.permission_id = p.id "
        "JOIN sys_roles r ON r.id = rp.role_id "
        "JOIN sys_user_roles ur ON ur.role_id = r.id "
        "WHERE ur.user_id = $1 AND r.status = 1 AND r.deleted = FALSE "
        "AND p.status = 1 AND p.perm_code NOT LIKE 'menu:%'",
        [userId](const drogon::orm::Result& r) {
            std::set<std::string> perms;
            for (const auto& row : r)
                perms.insert(row["perm_code"].as<std::string>());
            // 回填用集合: 空权限写哨兵, 读时过滤
            std::set<std::string> storePerms = perms;
            if (storePerms.empty())
                storePerms.insert(kEmptyPermSentinel);

            // 回填 Redis (SET + SADD pipeline; 失败不影响本次请求)
            // 注意: perms 为栈上局部变量, 异步回填必须按值捕获 (按值拷贝捕获后 onOk 再 move);
            // execCommandAsync 为 C 变参, %s 必须传 C 字符串 (.c_str()), 传 std::string 属 UB
            try {
                auto rdb = drogon::app().getRedisClient();
                auto key = permKey(userId);
                rdb->execCommandAsync(
                    [key, storePerms](const drogon::nosql::RedisResult&) {
                        // 追加成员
                        auto rdb2 = drogon::app().getRedisClient();
                        for (const auto& p : storePerms) {
                            rdb2->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                                                   [](const drogon::nosql::RedisException&) {},
                                                   "SADD %s %s", key.c_str(), p.c_str());
                        }
                        rdb2->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                                               [](const drogon::nosql::RedisException&) {},
                                               "EXPIRE %s %d", key.c_str(), kPermCacheTtlSec);
                    },
                    [](const drogon::nosql::RedisException&) {}, "DEL %s", key.c_str());
            } catch (...) {
            }
            forwardResult(userId, &perms, nullptr);
        },
        [userId](const drogon::orm::DrogonDbException& e) {
            forwardResult(userId, nullptr, &e.base());
        },
        SqlArg(userId));
}

void loadFromDb(int64_t userId, PermCallback onOk, ErrCallback onErr) {
    bool leader = false;
    {
        std::lock_guard lk(g_flightMtx);
        if (!g_inflight)
            g_inflight =
                new std::unordered_map<int64_t, std::vector<std::pair<PermCallback, ErrCallback>>>;
        auto& slot = (*g_inflight)[userId];
        leader = slot.empty();
        slot.emplace_back(std::move(onOk), std::move(onErr));
    }
    if (leader)
        doLoadFromDb(userId);
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
            perms.erase(kEmptyPermSentinel); // 过滤空权限哨兵
            onOk(perms);
        },
        [onErr, userId](const drogon::nosql::RedisException&) {
            // Redis 不可用时降级直查 DB, 保证可用性
            loadFromDb(userId, [onErr](const std::set<std::string>&) {}, std::move(onErr));
        },
        "SMEMBERS %s", permKey(userId).c_str());
}

void invalidateUserPerm(int64_t userId) {
    {
        std::lock_guard lk(g_scopeMtx);
        g_scopeCache.erase(userId);
    }
    try {
        auto rdb = drogon::app().getRedisClient();
        rdb->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                              [](const drogon::nosql::RedisException& e) {
                                  LOG_ERROR << "invalidate perm cache failed: " << e.what();
                              },
                              "DEL %s", permKey(userId).c_str());
    } catch (const std::exception& e) {
        LOG_ERROR << "invalidate perm cache failed: " << e.what();
    }
}

void loadMergedScopeAsync(int64_t userId, std::function<void(const ScopeResult&)> onOk,
                          ErrCallback onErr) {
    std::optional<ScopeResult> cached;
    {
        std::lock_guard lk(g_scopeMtx);
        auto it = g_scopeCache.find(userId);
        if (it != g_scopeCache.end()) {
            if (std::chrono::steady_clock::now() <= it->second.expireAt)
                cached = it->second.scope;
            else
                g_scopeCache.erase(it);
        }
    }
    if (cached) {
        onOk(*cached); // 命中: 同步回调 (锁外执行), 调用方均在异步上下文中调用
        return;
    }
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
                    [userId, res, onOk](const drogon::orm::Result& r2) mutable {
                        for (const auto& row : r2)
                            res.customDeptIds.push_back(row["dept_id"].as<int64_t>());
                        {
                            std::lock_guard lk(g_scopeMtx);
                            g_scopeCache[userId] = {res,
                                                    std::chrono::steady_clock::now() +
                                                        std::chrono::seconds(kScopeCacheTtlSec)};
                        }
                        onOk(res);
                    },
                    [onErr](const drogon::orm::DrogonDbException& e) { onErr(e.base()); },
                    SqlArg(userId));
                return;
            }
            {
                std::lock_guard lk(g_scopeMtx);
                g_scopeCache[userId] = {res, std::chrono::steady_clock::now() +
                                                 std::chrono::seconds(kScopeCacheTtlSec)};
            }
            onOk(res);
        },
        [onErr = std::move(onErr)](const drogon::orm::DrogonDbException& e) { onErr(e.base()); },
        SqlArg(userId));
}

} // namespace mes::RbacService
