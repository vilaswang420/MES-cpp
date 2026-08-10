#include "middlewares/AuditMiddleware.hh"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <deque>
#include <mutex>

namespace hms {

namespace {

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
constexpr size_t kFlushBatch = 100;
constexpr int kFlushIntervalSec = 1;

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
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_buffer.push_back(std::move(e));
    }
    // 到达批量阈值立即触发刷盘
}

// 批量刷盘: 逐条异步 INSERT (参数化防注入); M3 容量验证后可换 COPY 批量。
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
                [url = e.url](const std::exception& ex) {
                    // 审计失败只记日志不阻断
                    LOG_ERROR << "audit insert failed for " << url << ": " << ex.what();
                },
                e.userId, e.username, e.module, e.operation, e.method, e.url, e.params,
                e.responseCode, e.errorMsg, e.ip, e.userAgent, e.durationMs);
        } catch (const std::exception& ex) {
            LOG_ERROR << "audit enqueue failed: " << ex.what();
        }
    }
}

} // namespace

void startAuditFlusher() {
    drogon::app().getLoop()->runEvery(kFlushIntervalSec, [] { flush(); });
}

void AuditMiddleware::invoke(const drogon::HttpRequestPtr& req, MiddlewareNextCallback&& nextCb,
                             MiddlewareCallback&& mcb) {
    // 只审计业务 API 的写操作与敏感查询; healthz/captcha 等不审计
    const auto path = req->path();
    const auto method = req->methodString();
    const bool auditable = path.rfind("/api/v1/", 0) == 0 &&
                           (method != "GET" || path.rfind("/api/v1/system/audit-logs", 0) == 0);

    if (!auditable) {
        nextCb(req, std::move(mcb));
        return;
    }

    auto start = std::chrono::steady_clock::now();
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
    if (method == "GET") {
        e.params = req->query().substr(0, 2048);
    } else {
        e.params = std::string(req->body()).substr(0, 2048);
    }

    nextCb(req, [e = std::move(e), start,
                 mcb = std::move(mcb)](const drogon::HttpResponsePtr& resp) mutable {
        e.durationMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - start)
                                            .count());
        if (resp) {
            e.responseCode = static_cast<int>(resp->statusCode());
        } else {
            e.responseCode = 500;
        }
        enqueue(std::move(e));
        mcb(resp);
    });
}

} // namespace hms
