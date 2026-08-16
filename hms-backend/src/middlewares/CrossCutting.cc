#include "middlewares/CrossCutting.hh"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <deque>
#include <mutex>
#include <random>
#include <sstream>

#include "common/ApiResponse.hh"
#include "common/SqlParam.hh"
#include "middlewares/perm_routes.hh"
#include "services/RbacService.hh"
#include "utils/AuditMask.hh"
#include "utils/JwtUtils.hh"
#include "utils/Metrics.hh"

namespace hms {

namespace {

int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ---------- JWT 认证 (原 JwtMiddleware, 设计文档 5.3) ----------
// 白名单跳过 -> 提取 Bearer -> 验签+过期 -> Redis 黑名单 -> 注入用户上下文
void jwtCheck(const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& acb,
              drogon::AdviceChainCallback&& accb) {
    const auto path = req->path();
    const std::string method = req->methodString();
    const auto traceId = traceIdOf(req);

    auto reject = [&acb, traceId](const std::string& msg) {
        acb(ApiResponse::error(401, msg, traceId));
    };

    // 1. 公开白名单直接放行
    if (PermRoutes::isPublicPath(path, method)) {
        accb();
        return;
    }

    // 1.1 refresh 接口以 refresh_token 自证身份 (典型场景: access 已过期),
    // 豁免 access token 校验; 权限层仍要求 auth:bearer 映射存在
    if (path == "/api/v1/auth/refresh" && method == "POST") {
        accb();
        return;
    }

    // 2. 提取 Bearer Token
    auto authHeader = req->getHeader("Authorization");
    if (authHeader.size() <= 7 || authHeader.substr(0, 7) != "Bearer ") {
        reject("缺少认证令牌");
        return;
    }
    std::string token = authHeader.substr(7);

    // 3. 验证 JWT 签名与过期
    auto payload = JwtUtils::verifyAccessToken(token);
    if (!payload) {
        reject("令牌无效或已过期");
        return;
    }

    // 4. 检查 Redis 黑名单 (会话注销后失效)
    // acb/accb 转入异步回调, 需以 shared_ptr 持有 (同步拒绝路径已在上方消费 acb)
    auto acbPtr = std::make_shared<drogon::AdviceCallback>(std::move(acb));
    auto rdb = drogon::app().getRedisClient();
    auto sessionId = payload->sessionId;
    rdb->execCommandAsync(
        [req, payload, accb = std::move(accb), acbPtr,
         traceId](const drogon::nosql::RedisResult& result) mutable {
            // EXISTS 返回 1 表示会话已注销
            if (result.asInteger() > 0) {
                (*acbPtr)(ApiResponse::error(401, "会话已注销", traceId));
                return;
            }
            // 5. 注入用户上下文 (合成后的 data_scope, 见 5.4)
            auto attrs = req->getAttributes();
            attrs->insert("current_user_id", payload->userId);
            attrs->insert("current_username", payload->username);
            attrs->insert("current_dept_id", payload->deptId);
            attrs->insert("current_roles", payload->roles);
            attrs->insert("data_scope", payload->dataScope);
            attrs->insert("custom_dept_ids", payload->customDeptIds);
            attrs->insert("session_id", payload->sessionId);
            accb();
        },
        [acbPtr, traceId](const drogon::nosql::RedisException& e) {
            // Redis 异常时 fail-closed: 拒绝而非放行
            LOG_ERROR << "redis blacklist check failed: " << e.what();
            (*acbPtr)(ApiResponse::error(401, "认证服务暂不可用", traceId));
        },
        "EXISTS jwt:blacklist:%s", sessionId.c_str());
}

// ---------- RBAC 权限检查 (原 RbacMiddleware, fail-closed) ----------
// 公开接口显式白名单放行; 未注册权限映射的路由一律 403
void rbacCheck(const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& acb,
               drogon::AdviceChainCallback&& accb) {
    const auto path = req->path();
    const std::string method = req->methodString();
    const auto traceId = traceIdOf(req);

    // 1. 公开接口通过显式白名单放行 (与 jwtCheck 的 isPublicPath 一致)
    if (PermRoutes::isPublicPath(path, method)) {
        accb();
        return;
    }

    // 2. 解析当前路由所需权限
    auto requiredPerm = PermRoutes::getPermission(path, method);

    // 3. fail-closed: 未注册权限映射的路由一律拒绝
    //    防止新增/遗漏配置的接口对所有登录用户开放
    if (requiredPerm.empty()) {
        LOG_WARN << "fail-closed: route without perm mapping: " << method << " " << path;
        acb(ApiResponse::error(403, "路由未配置权限映射", traceId));
        return;
    }

    // 4. "auth:bearer" 表示仅需登录即可 (认证类自身接口, jwtCheck 已验证身份)
    if (requiredPerm == "auth:bearer") {
        accb();
        return;
    }

    // 5. 获取用户权限集 (Redis 缓存 perm:user:{userId}, 主动失效)
    // acb/accb 转入异步回调, 需以 shared_ptr 持有
    auto acbPtr = std::make_shared<drogon::AdviceCallback>(std::move(acb));
    auto attrs = req->getAttributes();
    auto userId = attrs->get<int64_t>("current_user_id");
    RbacService::getUserPermissionsAsync(
        userId,
        [req, requiredPerm, accb = std::move(accb), acbPtr,
         traceId](const std::set<std::string>& perms) mutable {
            if (perms.find(requiredPerm) == perms.end()) {
                (*acbPtr)(ApiResponse::error(403, "无权限: " + requiredPerm, traceId));
                return;
            }
            // 6. 记录本次路由所需权限, 供审计使用
            req->getAttributes()->insert("required_perm", requiredPerm);
            accb();
        },
        [acbPtr, traceId](const std::exception& e) {
            // 权限查询失败 fail-closed
            LOG_ERROR << "load user permissions failed: " << e.what();
            (*acbPtr)(ApiResponse::error(500, "权限服务暂不可用", traceId));
        });
}

// ---------- 审计 (原 AuditMiddleware, 计划任务 10) ----------
struct AuditEntry {
    int64_t userId = 0;
    std::string username;
    std::string module;    // 按路径第二段推断: system / production / auth
    std::string operation; // HTTP 方法映射: GET->QUERY, POST->CREATE, PUT->UPDATE, DELETE->DELETE
    std::string method;
    std::string url;
    std::string params; // 请求参数 JSON (截断 2KB, 避免大报文)
    int responseCode = 200;
    std::string errorMsg;
    std::string ip;
    std::string userAgent;
    int durationMs = 0;
};

std::mutex g_mu;
std::deque<AuditEntry> g_buffer;

std::string moduleOf(const std::string& path) {
    // /api/v1/{module}/...
    size_t p3 = 0;
    for (int i = 0; i < 3; ++i)
        p3 = path.find('/', p3 + 1);
    if (p3 == std::string::npos)
        return "other";
    auto end = path.find('/', p3 + 1);
    return path.substr(p3 + 1, end == std::string::npos ? end : end - p3 - 1);
}

std::string operationOf(const std::string& method) {
    if (method == "GET")
        return "QUERY";
    if (method == "POST")
        return "CREATE";
    if (method == "PUT")
        return "UPDATE";
    if (method == "DELETE")
        return "DELETE";
    return method;
}

void enqueue(AuditEntry e) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_buffer.push_back(std::move(e));
}

// 批量刷盘: 逐条异步 INSERT (参数化防注入); M3 容量验证后可换 COPY 批量
// 失败只记日志不阻断 —— 审计不可影响业务可用性
void flush() {
    std::deque<AuditEntry> batch;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_buffer.empty())
            return;
        batch.swap(g_buffer);
    }

    auto db = drogon::app().getDbClient();
    for (const auto& e : batch) {
        try {
            db->execSqlAsync(
                "INSERT INTO sys_audit_logs(user_id, username, module, operation, method, "
                "request_url, request_params, response_code, error_msg, ip_address, "
                "user_agent, duration_ms) "
                "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,NULLIF($9,''),$10,$11,$12)",
                [](const drogon::orm::Result&) {},
                [url = e.url](const drogon::orm::DrogonDbException& ex) {
                    // 审计失败只记日志不阻断
                    LOG_ERROR << "audit insert failed for " << url << ": " << ex.base().what();
                },
                e.userId, e.username, e.module, e.operation, e.method, e.url, e.params,
                SqlArg(e.responseCode), e.errorMsg, e.ip, e.userAgent, SqlArg(e.durationMs));
        } catch (const std::exception& ex) {
            LOG_ERROR << "audit enqueue failed: " << ex.what();
        }
    }
}

// 仅审计业务 API 的写操作与敏感查询; healthz/captcha 等不审计
bool auditable(const std::string& path, const std::string& method) {
    return path.rfind("/api/v1/", 0) == 0 &&
           (method != "GET" || path.rfind("/api/v1/system/audit-logs", 0) == 0);
}

void recordAudit(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
    const std::string path = req->path();
    const std::string method = req->methodString();
    if (!auditable(path, method))
        return;

    AuditEntry e;
    e.method = method;
    e.url = path.substr(0, 512);
    e.module = moduleOf(path);
    e.operation = operationOf(method);
    e.ip = req->peerAddr().toIp();
    e.userAgent = req->getHeader("user-agent").substr(0, 512);
    if (auto attrs = req->getAttributes(); attrs && attrs->find("current_user_id")) {
        e.userId = attrs->get<int64_t>("current_user_id");
        e.username = attrs->get<std::string>("current_username");
    }
    // 请求参数 (GET 取 query, 写操作取 body 摘要), 截断 2KB
    // P3-4.5: 写操作 body 经 JSON 脱敏 (password/newPassword/oldPassword -> ***),
    //         登录/改密明文密码不再落库 sys_audit_logs.request_params
    if (method == "GET") {
        e.params = req->query().substr(0, 2048);
    } else {
        e.params = AuditMask::maskSensitiveJson(std::string(req->body())).substr(0, 2048);
    }
    if (auto attrs = req->getAttributes(); attrs && attrs->find("req_start_us")) {
        e.durationMs = static_cast<int>((nowUs() - attrs->get<int64_t>("req_start_us")) / 1000);
    }
    e.responseCode = resp ? static_cast<int>(resp->statusCode()) : 500;
    enqueue(std::move(e));
}

} // namespace

// ---------- HTTP 指标 (计划任务 28): QPS/状态码/延迟直方图 ----------
// 在 preSendingAdvice 埋点: 覆盖成功与拦截响应 (401/403/404 同样计入)
void recordHttpMetrics(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
    if (!req || !resp)
        return;
    const std::string method = req->methodString();
    const int status = static_cast<int>(resp->statusCode());
    Metrics::counterInc("hms_http_requests_total{method=\"" + method + "\",status=\"" +
                        std::to_string(status) + "\"}");
    if (auto attrs = req->getAttributes(); attrs && attrs->find("req_start_us")) {
        double ms = (nowUs() - attrs->get<int64_t>("req_start_us")) / 1000.0;
        Metrics::histogramObserve("hms_http_request_duration_ms", ms);
    }
}

std::string genTraceId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << rng() << rng();
    return oss.str().substr(0, 24);
}

void installCrossCutting() {
    // Trace 观察者: 注入 trace_id 与请求开始时间 (advice 链执行前生效)
    drogon::app().registerPreHandlingAdvice([](const drogon::HttpRequestPtr& req) {
        auto traceId = req->getHeader("X-Trace-Id");
        if (traceId.empty())
            traceId = genTraceId();
        req->getAttributes()->insert("trace_id", traceId);
        req->getAttributes()->insert("req_start_us", nowUs());
    });

    // 链式 advice 按注册顺序执行: Jwt -> Rbac (拦截时调用 acb 直接回响应)
    drogon::app().registerPreHandlingAdvice(
        [](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& acb,
           drogon::AdviceChainCallback&& accb) { jwtCheck(req, std::move(acb), std::move(accb)); });
    drogon::app().registerPreHandlingAdvice([](const drogon::HttpRequestPtr& req,
                                               drogon::AdviceCallback&& acb,
                                               drogon::AdviceChainCallback&& accb) {
        rbacCheck(req, std::move(acb), std::move(accb));
    });

    // 审计: 仅成功到达 handler 的响应 (拦截响应在原洋葱模型中同样不经过 Audit)
    drogon::app().registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
            recordAudit(req, resp);
        });

    // 回写 X-Trace-Id 响应头 (ApiResponse 信封已自带, 此处兜底非信封响应);
    // 同时埋点 HTTP 指标 (任务 28): 所有出站响应都经过本 advice
    drogon::app().registerPreSendingAdvice(
        [](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
            recordHttpMetrics(req, resp);
            if (resp && req->getAttributes() && req->getAttributes()->find("trace_id"))
                resp->addHeader("X-Trace-Id", req->getAttributes()->get<std::string>("trace_id"));
        });
}

void startAuditFlusher() {
    constexpr int kFlushIntervalSec = 1;
    drogon::app().getLoop()->runEvery(kFlushIntervalSec, [] { flush(); });
}

} // namespace hms
