#include "services/AuthService.hh"

#include <drogon/drogon.h>

#include <random>

#include "services/RbacService.hh"
#include "utils/CryptoUtils.hh"
#include "utils/JwtUtils.hh"
#include "utils/TimeUtils.hh"

namespace hms::AuthService {

namespace {

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
                                       "DEL captcha:%s", captchaId);
                if (stored.empty() || stored != captchaCode)
                    return onErr(400, "验证码错误");
                // 验证码通过后继续账号校验 (复用无验证码路径)
                login(nlohmann::json{{"username", username}, {"password", password}}, clientIp,
                      std::move(onOk), std::move(onErr));
            },
            [onErr](const drogon::nosql::RedisException&) { onErr(500, "认证服务暂不可用"); },
            "GET captcha:%s", captchaId);
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

            if (!CryptoUtils::verifyPassword(password, row["password_hash"].as<std::string>())) {
                // 失败计数 +1, 达 5 次锁定 (auth.login.max_fail)
                auto db2 = drogon::app().getDbClient();
                db2->execSqlAsync(
                    "UPDATE sys_users SET login_fail_count = login_fail_count + 1, "
                    "status = CASE WHEN login_fail_count + 1 >= 5 THEN 2 ELSE status END "
                    "WHERE id = $1",
                    [](const drogon::orm::Result&) {}, [](const drogon::orm::DrogonDbException&) {},
                    userId);
                return onErr(401, "用户名或密码错误");
            }

            // 密码正确: 重置失败计数, 记录登录信息
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "UPDATE sys_users SET login_fail_count = 0, last_login_at = NOW(), "
                "last_login_ip = $2 WHERE id = $1",
                [](const drogon::orm::Result&) {}, [](const drogon::orm::DrogonDbException&) {},
                userId, clientIp);

            // 合成多角色 data_scope (5.4 节: 取最宽) 后签发 JWT
            RbacService::loadMergedScopeAsync(
                userId,
                [userId, row, onOk](const RbacService::ScopeResult& scope) mutable {
                    issueTokens(userId, row["username"].as<std::string>(),
                                row["dept_id"].isNull() ? 0 : row["dept_id"].as<int64_t>(),
                                row["dept_name"].as<std::string>(), scope, std::move(onOk));
                },
                [onErr](const std::exception& e) { onErr(500, e.what()); });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, username);
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
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, userId);
        },
        [onErr](const drogon::nosql::RedisException&) { onErr(500, "认证服务暂不可用"); },
        "EXISTS jwt:blacklist:%s", sessionId);
}

void logout(const std::string& sessionId, int64_t refreshExpireIn, JsonCb onOk, ErrCb onErr) {
    // 会话黑名单: TTL 覆盖 refresh_token 剩余寿命
    auto rdb = drogon::app().getRedisClient();
    rdb->execCommandAsync(
        [onOk](const drogon::nosql::RedisResult&) { onOk(nlohmann::json{{"logout", true}}); },
        [onErr](const drogon::nosql::RedisException& e) { onErr(500, e.what()); },
        "SET jwt:blacklist:%s 1 EX %lld", sessionId, refreshExpireIn);
}

void profile(int64_t userId, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT u.id, u.username, u.real_name, u.employee_no, u.email, u.phone, u.gender, "
        "u.last_login_at, COALESCE(d.dept_name,'') AS dept_name "
        "FROM sys_users u LEFT JOIN sys_departments d ON d.id = u.dept_id WHERE u.id = $1",
        [userId, onOk](const drogon::orm::Result& r) {
            if (r.empty())
                return;
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
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, userId);
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
            if (!CryptoUtils::verifyPassword(oldPwd, r[0]["password_hash"].as<std::string>()))
                return onErr(400, "原密码错误");
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "UPDATE sys_users SET password_hash = $2, password_changed_at = NOW() "
                "WHERE id = $1",
                [onOk](const drogon::orm::Result&) { onOk(nlohmann::json{{"changed", true}}); },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, userId,
                CryptoUtils::hashPassword(newPwd));
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, userId);
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
        "SET captcha:%s %s EX 300", id, code);
}

} // namespace hms::AuthService
