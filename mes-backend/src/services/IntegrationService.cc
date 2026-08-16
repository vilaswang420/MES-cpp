#include "services/IntegrationService.hh"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "common/ApiResponse.hh"
#include "common/SqlParam.hh"
#include "models/WorkOrderStateMachine.hh"
#include "mq/OutboxDispatcher.hh"
#include "utils/CircuitBreaker.hh"
#include "utils/IntegRules.hh"
#include "utils/TimeUtils.hh"

// ERP/WMS 集成实现 (计划任务 23)。
// 外呼链路: 熔断器 -> drogon HttpClient (sendRequestCoro) -> 记 integ_sync_logs;
// 失败按 integ_api_configs.retry_count 短退避自动重试, 仍失败记失败日志可经
// POST /integration/logs/{id}/retry 人工重发。
namespace mes::IntegrationService {

namespace {

struct ApiConfig {
    int64_t id = 0;
    std::string baseUrl;
    std::string token;
    int timeoutMs = 10000;
    int retryCount = 3;
};

// URL 查询参数编码 (RFC 3986): 保留字符除外全部 percent-encode
// (syncErpOrders 的日期/订单参数可能含空格/特殊字符, 直接拼接会破坏请求)
std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

// 外呼结果 (不抛异常, 由调用方决定补偿/响应)
struct ExtResult {
    bool ok = false;
    bool circuitOpen = false;
    int httpStatus = 0;
    int64_t durationMs = 0;
    nlohmann::json body;
    std::string err;
};

// ---- 熔断器: 每个外部系统一个 (设计文档 7.6, 成功重置计数版) ----
// CircuitBreaker 内含 mutex 不可拷贝/移动, map 存 unique_ptr 懒创建
CircuitBreaker& breakerOf(const std::string& systemType) {
    static std::map<std::string, std::unique_ptr<CircuitBreaker>> g_map;
    static std::mutex g_mu;
    std::lock_guard<std::mutex> lk(g_mu);
    auto& slot = g_map[systemType];
    if (!slot)
        slot = std::make_unique<CircuitBreaker>();
    return *slot;
}

drogon::Task<std::optional<ApiConfig>> loadConfig(const std::string& systemType) {
    auto db = drogon::app().getDbClient();
    auto r = co_await db->execSqlCoro(
        "SELECT id, base_url, COALESCE(token_key, '') AS token_key, timeout_ms, retry_count "
        "FROM integ_api_configs WHERE system_type = $1 AND enabled = TRUE LIMIT 1",
        systemType);
    if (r.empty())
        co_return std::nullopt;
    ApiConfig c;
    c.id = r[0]["id"].as<int64_t>();
    c.baseUrl = r[0]["base_url"].as<std::string>();
    c.token = r[0]["token_key"].as<std::string>();
    c.timeoutMs = r[0]["timeout_ms"].as<int>();
    c.retryCount = r[0]["retry_count"].as<int>();
    co_return c;
}

ApiConfig requireConfig(const std::optional<ApiConfig>& cfg, const std::string& systemType) {
    if (!cfg)
        throw Conflict(systemType + " 集成未配置或已停用 (integ_api_configs)");
    return *cfg;
}

// ---- 同步日志落库 (direction: 1 接收 2 发送; url 存 "METHOD path" 便于重发) ----
drogon::Task<int64_t> writeLog(const std::string& systemType, int direction,
                               const std::string& syncType, int64_t businessId,
                               const std::string& url, const std::string& reqBody,
                               const std::string& respBody, int httpStatus, int64_t durationMs,
                               bool ok, int retryCount, const std::string& errMsg) {
    auto db = drogon::app().getDbClient();
    auto r = co_await db->execSqlCoro(
        "INSERT INTO integ_sync_logs (system_type, sync_direction, sync_type, business_id, "
        "request_url, request_body, response_body, http_status, duration_ms, status, "
        "retry_count, error_msg) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12) RETURNING id",
        systemType, SqlArg(direction), syncType, SqlArg(businessId), url, reqBody, respBody,
        SqlArg(httpStatus), SqlArg(durationMs), SqlArg(ok ? 1 : 0), SqlArg(retryCount), errMsg);
    co_return r[0]["id"].as<int64_t>();
}

// ---- 外呼核心: 熔断 -> 重试 -> HttpClient; 不抛异常 ----
drogon::Task<ExtResult> callExternal(const ApiConfig& cfg, const std::string& systemType,
                                     const std::string& method, const std::string& path,
                                     const nlohmann::json& payload) {
    ExtResult res;
    auto& breaker = breakerOf(systemType);
    if (!breaker.allowRequest()) {
        res.circuitOpen = true;
        res.err = systemType + " 熔断器开启, 快速失败";
        co_return res;
    }

    // 注: Windows <windows.h> 的 max 宏污染, std::max 需加括号
    const int attempts = (std::max)(1, cfg.retryCount);
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto t0 = std::chrono::steady_clock::now();
        try {
            auto client = drogon::HttpClient::newHttpClient(cfg.baseUrl);
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setMethod(method == "GET" ? drogon::Get : drogon::Post);
            req->setPath(path);
            if (!payload.is_null()) {
                req->setBody(payload.dump());
                req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            }
            if (!cfg.token.empty())
                req->addHeader("Authorization", std::string("Bearer ") + cfg.token);
            auto resp = co_await client->sendRequestCoro(req, cfg.timeoutMs / 1000.0);
            res.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
            res.httpStatus = static_cast<int>(resp->getStatusCode());
            try {
                res.body = nlohmann::json::parse(resp->getBody());
            } catch (...) {
                res.body = nlohmann::json();
            }
            if (res.httpStatus >= 200 && res.httpStatus < 300) {
                breaker.recordSuccess();
                res.ok = true;
                co_return res;
            }
            res.err = std::string("HTTP ") + std::to_string(res.httpStatus);
        } catch (const std::exception& e) {
            res.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
            res.err = e.what();
        }
        breaker.recordFailure();
        if (attempt + 1 < attempts)
            co_await drogon::sleepCoro(drogon::app().getLoop(), 0.2);
    }
    co_return res;
}

// 外呼失败统一处理: 记失败日志 + 抛 502
drogon::Task<> failExternal(const std::string& systemType, int direction,
                            const std::string& syncType, int64_t businessId, const std::string& url,
                            const std::string& reqBody, const ExtResult& r) {
    co_await writeLog(systemType, direction, syncType, businessId, url, reqBody,
                      r.body.is_null() ? std::string() : r.body.dump(), r.httpStatus, r.durationMs,
                      false, 0, r.err);
    throw ApiException(502, systemType + " 调用失败: " + r.err);
}

} // namespace

nlohmann::json breakerStates() {
    return {{"ERP", breakerOf("ERP").stateName()}, {"WMS", breakerOf("WMS").stateName()}};
}

// ---- ERP 订单同步: GET {erp}/orders?窗口 -> upsert integ_erp_orders ----
drogon::Task<nlohmann::json> syncErpOrders(const nlohmann::json& body) {
    auto cfg = requireConfig(co_await loadConfig("ERP"), "ERP");
    auto startDate = body.value("start_date", std::string());
    auto endDate = body.value("end_date", std::string());
    if (startDate.empty() || endDate.empty())
        throw BadRequest("start_date 与 end_date 必填");
    auto orderType = body.value("order_type", 1);

    auto path = std::string("/erp/orders?start_date=") + urlEncode(startDate) +
                "&end_date=" + urlEncode(endDate) + "&order_type=" + std::to_string(orderType);
    auto r = co_await callExternal(cfg, "ERP", "GET", path, nullptr);
    if (!r.ok)
        co_await failExternal("ERP", 1, "sync_orders", 0, std::string("GET ") + path, "", r);

    auto db = drogon::app().getDbClient();
    auto orders = r.body.value("orders", nlohmann::json::array());
    int total = 0, inserted = 0, updated = 0;
    for (const auto& o : orders) {
        auto no = o.value("order_no", std::string());
        if (no.empty())
            continue;
        ++total;
        auto up = co_await db->execSqlCoro(
            "INSERT INTO integ_erp_orders (erp_order_no, erp_order_type, product_code, "
            "product_name, quantity, unit, plan_start_date, plan_end_date, priority, "
            "customer_name, status, raw_data, synced_at) "
            "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,0,$11,NOW()) "
            "ON CONFLICT (erp_order_no) DO UPDATE SET "
            "quantity = EXCLUDED.quantity, status = CASE WHEN integ_erp_orders.status = 0 "
            "THEN 0 ELSE integ_erp_orders.status END, raw_data = EXCLUDED.raw_data, "
            "synced_at = NOW() RETURNING (xmax = 0) AS inserted",
            no, SqlArg(o.value("order_type", orderType)), o.value("product_code", std::string()),
            o.value("product_name", std::string()), SqlArg(o.value("quantity", 0)),
            o.value("unit", std::string("PCS")), o.value("plan_start_date", startDate),
            o.value("plan_end_date", endDate), SqlArg(o.value("priority", 5)),
            o.value("customer_name", std::string()), o.dump());
        if (up[0]["inserted"].as<bool>())
            ++inserted;
        else
            ++updated;
    }

    auto logId = co_await writeLog("ERP", 1, "sync_orders", 0, std::string("GET ") + path, "",
                                   r.body.dump(), r.httpStatus, r.durationMs, true, 0, "");
    co_return nlohmann::json{{"total_synced", total},
                             {"new_orders", inserted},
                             {"updated_orders", updated},
                             {"failed", 0},
                             {"sync_log_id", logId}};
}

// ---- ERP 订单转工单 ----
drogon::Task<nlohmann::json> convertErpOrder(int64_t orderId,
                                             const WorkOrderService::UserCtx& ctx) {
    auto db = drogon::app().getDbClient();
    auto rows = co_await db->execSqlCoro(
        "SELECT id, erp_order_no, product_code, quantity, priority, status FROM "
        "integ_erp_orders WHERE id = $1",
        SqlArg(orderId));
    if (rows.empty())
        throw NotFound("ERP 订单不存在");
    if (rows[0]["status"].as<int>() != 0)
        throw Conflict("仅待同步状态的 ERP 订单可转工单");

    auto productCode = rows[0]["product_code"].as<std::string>();
    auto pr = co_await db->execSqlCoro(
        "SELECT id, process_id FROM prod_products WHERE product_code = $1 AND deleted = FALSE",
        productCode);
    if (pr.empty())
        throw BadRequest(std::string("产品 ") + productCode + " 不存在, 请先在 MES 建档");
    if (pr[0]["process_id"].isNull())
        throw BadRequest(std::string("产品 ") + productCode + " 未绑定工艺路线");

    nlohmann::json woBody = {
        {"product_id", pr[0]["id"].as<int64_t>()},
        {"process_id", pr[0]["process_id"].as<int64_t>()},
        {"plan_qty", rows[0]["quantity"].as<int>()},
        {"priority", rows[0]["priority"].as<int>()},
        {"remark", std::string("ERP 订单 ") + rows[0]["erp_order_no"].as<std::string>()}};
    auto wo = co_await WorkOrderService::create(woBody, ctx);

    co_await db->execSqlCoro("UPDATE integ_erp_orders SET status = 1, work_order_id = $1, "
                             "updated_at = NOW() WHERE id = $2",
                             SqlArg(wo["id"].get<int64_t>()), SqlArg(orderId));
    co_return nlohmann::json{{"erp_order_no", rows[0]["erp_order_no"].as<std::string>()},
                             {"work_order_id", wo["id"]},
                             {"work_order_no", wo["work_order_no"]}};
}

// ---- 完工回报 Saga (设计文档 7.5): T1 本地完工 -> T2 ERP 回报 -> T3 WMS 入库 ----
drogon::Task<nlohmann::json> reportCompletionSaga(int64_t workOrderId) {
    auto db = drogon::app().getDbClient();
    auto rows = co_await db->execSqlCoro(
        "SELECT wo.id, wo.work_order_no, wo.status, wo.completed_qty, wo.product_id, "
        "p.product_code "
        "FROM prod_work_orders wo LEFT JOIN prod_products p ON p.id = wo.product_id "
        "WHERE wo.id = $1",
        SqlArg(workOrderId));
    if (rows.empty())
        throw NotFound("工单不存在");
    if (rows[0]["status"].as<int>() != WorkOrderStateMachine::kInProgress)
        throw Conflict("仅进行中的工单可触发完工回报 Saga");
    auto woNo = rows[0]["work_order_no"].as<std::string>();
    auto completedQty = rows[0]["completed_qty"].isNull() ? 0 : rows[0]["completed_qty"].as<int>();
    auto productCode = rows[0]["product_code"].isNull() ? std::string()
                                                        : rows[0]["product_code"].as<std::string>();

    // T1: 本地事务置完工 (补偿: 回滚为进行中)
    // 与正常完工路径 (report 满量自动完工) 保持一致:
    // 走 3->5 状态机语义、写 actual_end_at、事务内写 stop_collection outbox
    auto trans = co_await db->newTransactionCoro();
    auto t1 = co_await trans->execSqlCoro(
        "UPDATE prod_work_orders SET status = $1, "
        "actual_end_at = COALESCE(actual_end_at, NOW()), updated_at = NOW() "
        "WHERE id = $2 AND status = $3",
        SqlArg(static_cast<int>(WorkOrderStateMachine::kCompleted)), SqlArg(workOrderId),
        SqlArg(static_cast<int>(WorkOrderStateMachine::kInProgress)));
    if (t1.affectedRows() == 0)
        throw Conflict("工单状态已变化, Saga 终止");
    nlohmann::json msg;
    msg["version"] = "1.0";
    msg["type"] = "stop_collection";
    msg["work_order_id"] = workOrderId;
    msg["work_order_no"] = woNo;
    msg["product_id"] = rows[0]["product_id"].isNull() ? 0 : rows[0]["product_id"].as<int64_t>();
    msg["timestamp"] = TimeUtils::nowUtcIso();
    co_await trans->execSqlCoro(OutboxService::kEnqueueSql, "iot.exchange", "cmd.stop_collection",
                                msg.dump());
    co_await commitAwait(std::move(trans));

    auto rollbackT1 = [&]() -> drogon::Task<> {
        try {
            co_await drogon::app().getDbClient()->execSqlCoro(
                "UPDATE prod_work_orders SET status = $1, updated_at = NOW() WHERE id = $2",
                SqlArg(static_cast<int>(WorkOrderStateMachine::kInProgress)), SqlArg(workOrderId));
        } catch (const std::exception& e) {
            LOG_ERROR << "[integ] saga T1 补偿失败, 需人工介入 wo=" << workOrderId << ": "
                      << e.what();
        }
    };

    // T2: ERP 完工回报 (补偿: 回报撤销)
    auto erpCfg = requireConfig(co_await loadConfig("ERP"), "ERP");
    auto erpPath = std::string("/erp/work-orders/") + woNo + "/completion-report";
    nlohmann::json erpPayload = {{"work_order_no", woNo}, {"completed_qty", completedQty}};
    auto t2 = co_await callExternal(erpCfg, "ERP", "POST", erpPath, erpPayload);
    if (!t2.ok) {
        co_await rollbackT1();
        co_await failExternal("ERP", 2, "completion_report", workOrderId,
                              std::string("POST ") + erpPath, erpPayload.dump(), t2);
    }

    // T3: WMS 成品入库 (补偿: 入库撤销)
    auto wmsCfg = requireConfig(co_await loadConfig("WMS"), "WMS");
    // 注: 必须显式 std::string, auto 推导 const char* 会导致 "POST " + wmsPath 指针加法
    std::string wmsPath = "/wms/stock-in";
    nlohmann::json wmsPayload = {{"work_order_no", woNo},
                                 {"product_code", productCode},
                                 {"quantity", completedQty},
                                 {"sync_type", 2}};
    auto t3 = co_await callExternal(wmsCfg, "WMS", "POST", wmsPath, wmsPayload);
    if (!t3.ok) {
        // 逆序补偿: T2 -> T1
        auto cancel = co_await callExternal(
            erpCfg, "ERP", "POST", std::string("/erp/work-orders/") + woNo + "/report-cancel",
            nlohmann::json{{"work_order_no", woNo}});
        if (!cancel.ok)
            LOG_ERROR << "[integ] saga T2 补偿失败, 需人工介入 wo=" << workOrderId << ": "
                      << cancel.err;
        co_await rollbackT1();
        co_await failExternal("WMS", 2, "stock_in", workOrderId, std::string("POST ") + wmsPath,
                              wmsPayload.dump(), t3);
    }

    // T4: 成品入库流水 + 成功日志
    co_await db->execSqlCoro(
        "INSERT INTO integ_wms_inventory (material_code, quantity, status, sync_type, "
        "work_order_id, raw_data, synced_at) VALUES ($1,$2,'INBOUND',2,$3,$4,NOW())",
        productCode, SqlArg(completedQty), SqlArg(workOrderId), t3.body.dump());
    auto logId = co_await writeLog("ERP", 2, "completion_report", workOrderId,
                                   std::string("POST ") + erpPath, erpPayload.dump(),
                                   t2.body.dump(), t2.httpStatus, t2.durationMs, true, 0, "");
    co_await writeLog("WMS", 2, "stock_in", workOrderId, std::string("POST ") + wmsPath,
                      wmsPayload.dump(), t3.body.dump(), t3.httpStatus, t3.durationMs, true, 0, "");

    co_return nlohmann::json{{"saga", "completed"},
                             {"work_order_id", workOrderId},
                             {"work_order_no", woNo},
                             {"sync_log_id", logId}};
}

// ---- WMS 领料 / 入库 (独立调用) ----
drogon::Task<nlohmann::json> wmsCall(const nlohmann::json& body, const std::string& action,
                                     int syncType) {
    auto materialCode = body.value("material_code", std::string());
    auto quantity = body.value("quantity", 0.0);
    if (materialCode.empty() || quantity <= 0)
        throw BadRequest("material_code 与 quantity 必填且数量大于 0");
    auto workOrderId = body.value("work_order_id", (int64_t)0);

    auto cfg = requireConfig(co_await loadConfig("WMS"), "WMS");
    auto path = std::string("/wms/") + action;
    nlohmann::json payload = {{"material_code", materialCode},
                              {"quantity", quantity},
                              {"work_order_id", workOrderId},
                              {"sync_type", syncType}};
    auto r = co_await callExternal(cfg, "WMS", "POST", path, payload);
    if (!r.ok)
        co_await failExternal("WMS", 2, action, workOrderId, std::string("POST ") + path,
                              payload.dump(), r);

    auto db = drogon::app().getDbClient();
    auto inv = co_await db->execSqlCoro(
        "INSERT INTO integ_wms_inventory (material_code, material_name, warehouse, quantity, "
        "unit, status, sync_type, work_order_id, raw_data, synced_at) "
        "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,NOW()) RETURNING id",
        materialCode, body.value("material_name", std::string()),
        body.value("warehouse", std::string()), SqlArg(quantity),
        body.value("unit", std::string("PCS")), syncType == 1 ? "ISSUED" : "INBOUND",
        SqlArg(syncType), SqlArg(workOrderId), r.body.dump());
    auto logId =
        co_await writeLog("WMS", 2, action, workOrderId, std::string("POST ") + path,
                          payload.dump(), r.body.dump(), r.httpStatus, r.durationMs, true, 0, "");
    co_return nlohmann::json{{"sync_log_id", logId}, {"inventory_id", inv[0]["id"].as<int64_t>()}};
}

drogon::Task<nlohmann::json> wmsPickRequest(const nlohmann::json& body) {
    co_return co_await wmsCall(body, "pick-request", 1);
}

drogon::Task<nlohmann::json> wmsStockIn(const nlohmann::json& body) {
    co_return co_await wmsCall(body, "stock-in", 2);
}

// ---- 同步日志 ----
nlohmann::json logArr(const drogon::orm::Result& rows) {
    auto arr = nlohmann::json::array();
    for (const auto& row : rows) {
        arr.push_back(
            {{"id", row["id"].as<int64_t>()},
             {"system_type", row["system_type"].as<std::string>()},
             {"sync_direction", row["sync_direction"].as<int>()},
             {"sync_type", row["sync_type"].as<std::string>()},
             {"business_id", row["business_id"].isNull() ? 0 : row["business_id"].as<int64_t>()},
             {"request_url", row["request_url"].as<std::string>()},
             {"http_status", row["http_status"].isNull() ? 0 : row["http_status"].as<int>()},
             {"duration_ms", row["duration_ms"].isNull() ? 0 : row["duration_ms"].as<int>()},
             {"status", row["status"].as<int>()},
             {"retry_count", row["retry_count"].as<int>()},
             {"error_msg",
              row["error_msg"].isNull() ? std::string() : row["error_msg"].as<std::string>()},
             {"created_at", row["created_at"].as<std::string>()}});
    }
    return arr;
}

drogon::Task<nlohmann::json> listLogs(int page, int pageSize, const std::string& systemType,
                                      int status) {
    if (page < 1)
        page = 1;
    if (pageSize < 1 || pageSize > 200)
        pageSize = 20;
    auto db = drogon::app().getDbClient();

    auto offset = (int64_t)(page - 1) * pageSize;
    // NULL 哨兵动态过滤: 固定 SQL 零拼接, systemType/status 任意组合均生效
    // (历史实现两个独立 if 分支, systemType=ERP 时 status 过滤被跳过; 判定规则见 IntegRules.hh)
    auto typeArg = IntegRules::validSystemType(systemType) ? SqlArg(systemType) : SqlArgNull();
    auto statusArg = IntegRules::validStatusFilter(status) ? SqlArg(status) : SqlArgNull();
    auto cntRow = co_await db->execSqlCoro(
        "SELECT COUNT(*) AS cnt FROM integ_sync_logs "
        "WHERE ($1::text IS NULL OR system_type = $1) AND ($2::int IS NULL OR status = $2)",
        typeArg, statusArg);
    auto total = cntRow[0]["cnt"].as<int64_t>();
    auto rows = co_await db->execSqlCoro(
        "SELECT id, system_type, sync_direction, sync_type, business_id, request_url, "
        "http_status, duration_ms, status, retry_count, error_msg, created_at "
        "FROM integ_sync_logs "
        "WHERE ($1::text IS NULL OR system_type = $1) AND ($2::int IS NULL OR status = $2) "
        "ORDER BY id DESC LIMIT $3 OFFSET $4",
        typeArg, statusArg, SqlArg(pageSize), SqlArg(offset));
    co_return nlohmann::json{{"total", total}, {"list", logArr(rows)}};
}

// ---- 失败日志重发 (request_url 存 "METHOD path", 原样重放) ----
drogon::Task<nlohmann::json> retryLog(int64_t logId) {
    auto db = drogon::app().getDbClient();
    auto rows = co_await db->execSqlCoro(
        "SELECT id, system_type, request_url, request_body, status FROM integ_sync_logs "
        "WHERE id = $1",
        SqlArg(logId));
    if (rows.empty())
        throw NotFound("同步日志不存在");
    if (rows[0]["status"].as<int>() == 1)
        throw Conflict("该日志已成功, 无需重试");

    auto systemType = rows[0]["system_type"].as<std::string>();
    auto cfg = requireConfig(co_await loadConfig(systemType), systemType);

    auto url = rows[0]["request_url"].as<std::string>();
    // "METHOD path" 解析 (规则见 IntegRules.hh, 无空格按历史语义默认 POST)
    auto [method, path] = IntegRules::parseMethodPath(url);
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(rows[0]["request_body"].as<std::string>());
    } catch (...) {
        payload = nullptr;
    }

    co_await db->execSqlCoro("UPDATE integ_sync_logs SET status = 2 WHERE id = $1", SqlArg(logId));
    auto r = co_await callExternal(cfg, systemType, method, path, payload);
    co_await db->execSqlCoro(
        "UPDATE integ_sync_logs SET status = $1, http_status = $2, duration_ms = $3, "
        "response_body = $4, error_msg = $5, retry_count = retry_count + 1 WHERE id = $6",
        SqlArg(r.ok ? 1 : 0), SqlArg(r.httpStatus), SqlArg(r.durationMs),
        r.body.is_null() ? std::string() : r.body.dump(), r.err, SqlArg(logId));
    if (!r.ok)
        throw ApiException(502, systemType + " 重试失败: " + r.err);
    co_return nlohmann::json{{"log_id", logId}, {"status", 1}, {"http_status", r.httpStatus}};
}

} // namespace mes::IntegrationService
