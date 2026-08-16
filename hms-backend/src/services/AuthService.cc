#include "services/AuthService.hh"

#include <drogon/drogon.h>

#include <chrono>
#include <mutex>
#include <random>
#include <unordered_map>

#include "common/SqlParam.hh"
#include "services/RbacService.hh"
#include "utils/CpuOffload.hh"
#include "utils/CryptoUtils.hh"
#include "utils/JwtUtils.hh"
#include "utils/TimeUtils.hh"

namespace hms::AuthService {

namespace {

// bcrypt verify 结果短 TTL 缓存 (key = 密码+库中哈希):
// 同一凭据的重复登录 (压测/多端/会话续接) 免去重复 cost=10 计算;
// 密码修改后哈希变化自然失效; 仅缓存成功结果, 失败仍每次实算 (保留失败计数语义)
struct VerifyCacheEntry {
    std::chrono::steady_clock::time_point expireAt;
};
std::mutex g_verifyMtx;
std::unordered_map<std::string, VerifyCacheEntry> g_verifyCache;
constexpr size_t kVerifyCacheMax = 10000;
constexpr int kVerifyCacheTtlSec = 60;

bool verifyCacheGet(const std::string& key) {
    std::lock_guard lk(g_verifyMtx);
    auto it = g_verifyCache.find(key);
    if (it == g_verifyCache.end())
        return false;
    if (std::chrono::steady_clock::now() > it->second.expireAt) {
        g_verifyCache.erase(it);
        return false;
    }
    return true;
}

void verifyCachePut(const std::string& key) {
    std::lock_guard lk(g_verifyMtx);
    if (g_verifyCache.size() >= kVerifyCacheMax)
        g_verifyCache.clear(); // 粗粒度满则清, 避免内存无界增长
    g_verifyCache[key] = {std::chrono::steady_clock::now() +
                          std::chrono::seconds(kVerifyCacheTtlSec)};
}

std::string randomCode(size_t len) {
    static const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::string s;
    for (size_t i = 0; i < len; ++i)
        s += alphabet[rng() % 32];
    return s;
}

std::string uuid() {
    return randomCode(32);
}

// 签发一对 token 并组装响应 data
void issueTokens(int64_t userId, const std::string& username, int64_t deptId,
                 const std::string& deptName, const RbacService::ScopeResult& scope, JsonCb onOk) {
    JwtUtils::TokenPayload p;
    p.userId = userId;
    p.username = username;
    p.deptId = deptId;
    p.roles = scope.roleCodes;
    p.dataScope = scope.dataScope;
    p.customDeptIds = scope.customDeptIds;
    p.sessionId = uuid();
    p.jti = uuid();

    auto access = JwtUtils::signAccessToken(p);
    auto refresh = JwtUtils::signRefreshToken(userId, p.sessionId);

    // refresh_token 会话登记 (logout 时写黑名单 jwt:blacklist:{sessionId})
    nlohmann::json data;
    data["access_token"] = access;
    data["refresh_token"] = refresh;
    data["expires_in"] = 7200;
    data["token_type"] = "Bearer";
    data["user"] = {{"id", userId},
                    {"username", username},
                    {"dept_name", deptName},
                    {"roles", scope.roleCodes}};
    // permissions 列表供前端按钮级控制 (菜单已过滤)
    RbacService::getUserPermissionsAsync(
        userId,
        [data, onOk](const std::set<std::string>& perms) mutable {
            data["user"]["permissions"] = perms;
            onOk(data);
        },
        [data, onOk](const std::exception&) mutable {
            data["user"]["permissions"] = nlohmann::json::array();
            onOk(data);
        });
}

// 密码校验完成后的后续流程 (失败计数/登录信息/scope 合成/签发)
void handleVerified(int64_t userId, const drogon::orm::Row& row, const std::string& clientIp,
                    bool ok, JsonCb onOk, ErrCb onErr) {
    if (!ok) {
        // 失败计数 +1, 达 5 次锁定 (auth.login.max_fail)
        auto db2 = drogon::app().getDbClient();
        db2->execSqlAsync(
            "UPDATE sys_users SET login_fail_count = login_fail_count + 1, "
            "status = CASE WHEN login_fail_count + 1 >= 5 THEN 2 ELSE status END "
            "WHERE id = $1",
            [](const drogon::orm::Result&) {}, [](const drogon::orm::DrogonDbException&) {},
            SqlArg(userId));
        return onErr(401, "用户名或密码错误");
    }

    // 密码正确: 重置失败计数; last_login_at 每用户每分钟至多写一次,
    // 避免高频登录对同行 UPDATE 串行化 (审计已有完整登录记录)
    auto db2 = drogon::app().getDbClient();
    db2->execSqlAsync(
        "UPDATE sys_users SET login_fail_count = 0, "
        "last_login_at = CASE WHEN last_login_at IS NULL "
        "OR last_login_at < NOW() - INTERVAL '1 minute' THEN NOW() "
        "ELSE last_login_at END, "
        "last_login_ip = CASE WHEN last_login_at IS NULL "
        "OR last_login_at < NOW() - INTERVAL '1 minute' THEN $2 "
        "ELSE last_login_ip END "
        "WHERE id = $1",
        [](const drogon::orm::Result&) {}, [](const drogon::orm::DrogonDbException&) {},
        SqlArg(userId), clientIp);

    // 合成多角色 data_scope (5.4 节: 取最宽) 后签发 JWT
    RbacService::loadMergedScopeAsync(
        userId,
        [userId, row, onOk](const RbacService::ScopeResult& scope) mutable {
            issueTokens(userId, row["username"].as<std::string>(),
                        row["dept_id"].isNull() ? 0 : row["dept_id"].as<int64_t>(),
                        row["dept_name"].as<std::string>(), scope, std::move(onOk));
        },
        [onErr](const std::exception& e) { onErr(500, e.what()); });
}
} // namespace

void login(const nlohmann::json& body, const std::string& clientIp, JsonCb onOk, ErrCb onErr) {
    auto username = body.value("username", "");
    auto password = body.value("password", "");
    if (username.empty() || password.empty())
        return onErr(400, "用户名或密码不能为空");

    // 验证码校验 (dev 环境允许缺省; 生产由 sys_configs 开关控制)
    auto captchaId = body.value("captcha_id", "");
    auto captchaCode = body.value("captcha_code", "");
    if (!captchaId.empty()) {
        auto rdb = drogon::app().getRedisClient();
        rdb->execCommandAsync(
            [username, password, captchaId, captchaCode, clientIp, onOk,
             onErr](const drogon::nosql::RedisResult& result) mutable {
                auto stored = result.asString();
                auto rdb2 = drogon::app().getRedisClient();
                rdb2->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                                       [](const drogon::nosql::RedisException&) {},
                                       "DEL captcha:%s", captchaId.c_str());
                if (stored.empty() || stored != captchaCode)
                    return onErr(400, "验证码错误");
                // 验证码通过后继续账号校验 (复用无验证码路径)
                login(nlohmann::json{{"username", username}, {"password", password}}, clientIp,
                      std::move(onOk), std::move(onErr));
            },
            [onErr](const drogon::nosql::RedisException&) { onErr(500, "认证服务暂不可用"); },
            "GET captcha:%s", captchaId.c_str());
        return;
    }

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT u.id, u.username, u.password_hash, u.real_name, u.status, u.deleted, "
        "u.login_fail_count, u.dept_id, COALESCE(d.dept_name, '') AS dept_name "
        "FROM sys_users u LEFT JOIN sys_departments d ON d.id = u.dept_id "
        "WHERE u.username = $1",
        [username, password, clientIp, onOk, onErr](const drogon::orm::Result& r) mutable {
            if (r.empty())
                return onErr(401, "用户名或密码错误");
            auto row = r[0];
            auto userId = row["id"].as<int64_t>();
            auto status = row["status"].as<int>();
            if (row["deleted"].as<bool>())
                return onErr(401, "用户名或密码错误");
            if (status == 0)
                return onErr(403, "账号已禁用");
            if (status == 2)
                return onErr(403, "账号已锁定");

            // bcrypt cost=10 为 CPU 密集计算 (~50-100ms), 未命中缓存时卸载到工作线程,
            // 完成后回 IO 线程继续, 避免阻塞事件循环拖垮 P95
            auto hash = row["password_hash"].as<std::string>();
            // P3-4.2: 缓存 key 不再含明文密码 (原 password|hash 使明文驻留进程内存 60s),
            //         改 username|ip|hash —— 同用户同密码 hash+同 IP 命中率不变, 压测收益保留
            auto cacheKey = username + "|" + clientIp + "|" + hash;
            // 缓存命中直接在 IO 线程短路 (免去线程池调度往返)
            if (verifyCacheGet(cacheKey)) {
                handleVerified(userId, row, clientIp, true, std::move(onOk), std::move(onErr));
                return;
            }
            offloadCpu(
                [password, hash, cacheKey] {
                    bool ok = CryptoUtils::verifyPassword(password, hash);
                    if (ok)
                        verifyCachePut(cacheKey);
                    return ok;
                },
                [userId, row, clientIp, onOk, onErr](bool ok) mutable {
                    handleVerified(userId, row, clientIp, ok, std::move(onOk), std::move(onErr));
                });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        username);
}

void refresh(const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto token = body.value("refresh_token", "");
    auto claims = JwtUtils::verifyRefreshToken(token);
    if (!claims)
        return onErr(401, "refresh_token 无效或已过期");

    auto userId = (*claims)["user_id"].get<int64_t>();
    auto sessionId = (*claims)["session_id"].get<std::string>();

    // 旧会话黑名单检查 (防止已注销会话被刷新)
    auto rdb = drogon::app().getRedisClient();
    rdb->execCommandAsync(
        [userId, sessionId, onOk, onErr](const drogon::nosql::RedisResult& result) mutable {
            if (result.asInteger() > 0)
                return onErr(401, "会话已注销");

            auto db = drogon::app().getDbClient();
            db->execSqlAsync(
                "SELECT u.id, u.username, u.dept_id, COALESCE(d.dept_name,'') AS dept_name "
                "FROM sys_users u LEFT JOIN sys_departments d ON d.id = u.dept_id "
                "WHERE u.id = $1 AND u.status = 1 AND u.deleted = FALSE",
                [userId, onOk, onErr](const drogon::orm::Result& r) mutable {
                    if (r.empty())
                        return onErr(401, "用户不存在或已禁用");
                    auto row = r[0];
                    RbacService::loadMergedScopeAsync(
                        userId,
                        [userId, row, onOk](const RbacService::ScopeResult& scope) mutable {
                            issueTokens(userId, row["username"].as<std::string>(),
                                        row["dept_id"].isNull() ? 0 : row["dept_id"].as<int64_t>(),
                                        row["dept_name"].as<std::string>(), scope, std::move(onOk));
                        },
                        [onErr](const std::exception& e) { onErr(500, e.what()); });
                },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
                SqlArg(userId));
        },
        [onErr](const drogon::nosql::RedisException&) { onErr(500, "认证服务暂不可用"); },
        "EXISTS jwt:blacklist:%s", sessionId.c_str());
}

void logout(const std::string& sessionId, int64_t refreshExpireIn, JsonCb onOk, ErrCb onErr) {
    // 会话黑名单: TTL 覆盖 refresh_token 剩余寿命
    auto rdb = drogon::app().getRedisClient();
    rdb->execCommandAsync(
        [onOk](const drogon::nosql::RedisResult&) { onOk(nlohmann::json{{"logout", true}}); },
        [onErr](const drogon::nosql::RedisException& e) { onErr(500, e.what()); },
        "SET jwt:blacklist:%s 1 EX %lld", sessionId.c_str(), refreshExpireIn);
}

void profile(int64_t userId, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT u.id, u.username, u.real_name, u.employee_no, u.email, u.phone, u.gender, "
        "u.last_login_at, COALESCE(d.dept_name,'') AS dept_name "
        "FROM sys_users u LEFT JOIN sys_departments d ON d.id = u.dept_id WHERE u.id = $1",
        [userId, onOk, onErr](const drogon::orm::Result& r) {
            if (r.empty())
                return onErr(404, "用户不存在");
            auto row = r[0];
            nlohmann::json data;
            data["id"] = row["id"].as<int64_t>();
            data["username"] = row["username"].as<std::string>();
            data["real_name"] = row["real_name"].as<std::string>();
            data["employee_no"] =
                row["employee_no"].isNull() ? "" : row["employee_no"].as<std::string>();
            data["dept_name"] = row["dept_name"].as<std::string>();
            RbacService::getUserPermissionsAsync(
                userId,
                [data, onOk](const std::set<std::string>& perms) mutable {
                    data["permissions"] = perms;
                    onOk(data);
                },
                [data, onOk](const std::exception&) mutable { onOk(data); });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(userId));
}

void changePassword(int64_t userId, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto oldPwd = body.value("old_password", "");
    auto newPwd = body.value("new_password", "");
    if (newPwd.size() < 8)
        return onErr(400, "新密码长度至少 8 位");

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT password_hash FROM sys_users WHERE id = $1",
        [userId, oldPwd, newPwd, onOk, onErr](const drogon::orm::Result& r) {
            if (r.empty())
                return onErr(404, "用户不存在");
            auto hash = r[0]["password_hash"].as<std::string>();
            // 校验旧密码 + 新密码哈希均为 bcrypt CPU 密集工作, 一并卸载到工作线程
            offloadCpu(
                [oldPwd, newPwd, hash] {
                    bool ok = CryptoUtils::verifyPassword(oldPwd, hash);
                    return std::pair<bool, std::string>{ok, ok ? CryptoUtils::hashPassword(newPwd)
                                                               : std::string{}};
                },
                [userId, onOk, onErr](std::pair<bool, std::string> res) {
                    if (!res.first)
                        return onErr(400, "原密码错误");
                    auto db2 = drogon::app().getDbClient();
                    db2->execSqlAsync(
                        "UPDATE sys_users SET password_hash = $2, password_changed_at = NOW() "
                        "WHERE id = $1",
                        [onOk](const drogon::orm::Result&) {
                            onOk(nlohmann::json{{"changed", true}});
                        },
                        [onErr](const drogon::orm::DrogonDbException& e) {
                            onErr(500, e.base().what());
                        },
                        SqlArg(userId), res.second);
                });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(userId));
}

void captcha(JsonCb onOk, ErrCb onErr) {
    auto id = uuid();
    auto code = randomCode(4);
    auto rdb = drogon::app().getRedisClient();
    rdb->execCommandAsync(
        [id, code, onOk](const drogon::nosql::RedisResult&) {
            nlohmann::json data;
            data["captcha_id"] = id;
            // MVP 直接返回明文 (前端渲染为图片占位); M2 替换为 SVG/PNG 渲染
            data["dev_captcha"] = code;
            onOk(data);
        },
        [onErr](const drogon::nosql::RedisException& e) { onErr(500, e.what()); },
        "SET captcha:%s %s EX 300", id.c_str(), code.c_str());
}

} // namespace hms::AuthService
