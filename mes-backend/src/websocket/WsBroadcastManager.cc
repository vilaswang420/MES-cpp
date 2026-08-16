#include "websocket/WsBroadcastManager.hh"

#include <drogon/drogon.h>
#include <hiredis/hiredis.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "utils/Metrics.hh"
#include "utils/TimeUtils.hh"

namespace mes::WsBroadcastManager {

namespace {

constexpr const char* kRedisPrefix = "ws:broadcast:";
constexpr double kMergeTickSec = 0.2; // 合并发送 tick = 5Hz 轮询
// 说明: drogon send 线程安全, 合并缓冲 + trantor 定时器轮询发送;
// 避免高频消息逐条直发导致连接压力
constexpr size_t kMaxSubsPerChannel = 2000;

const std::vector<std::string> kChannels = {"production.realtime", "device.status", "alert",
                                            "workorder.event"};

struct Subscriber {
    drogon::WebSocketConnectionPtr conn;
};

std::mutex g_mu;
std::unordered_map<std::string, std::vector<Subscriber>> g_subs;
// 待发合并: 同频道仅保留最新一条, tick 时批量发送
std::unordered_map<std::string, std::string> g_pending;

std::atomic<bool> g_stop{false};
std::thread g_redisThread;

std::string envelope(const std::string& channel, const nlohmann::json& payload) {
    nlohmann::json env = {{"version", "1.0"},
                          {"channel", channel},
                          {"ts", TimeUtils::nowUtcIso()},
                          {"payload", payload}};
    return env.dump();
}

void drainPending() {
    std::unordered_map<std::string, std::string> batch;
    {
        std::lock_guard lk(g_mu);
        if (g_pending.empty())
            return;
        batch.swap(g_pending);
    }
    for (const auto& [channel, env] : batch) {
        std::vector<Subscriber> snapshot;
        {
            std::lock_guard lk(g_mu);
            auto it = g_subs.find(channel);
            if (it == g_subs.end() || it->second.empty())
                continue;
            snapshot = it->second;
        }
        for (auto& sub : snapshot) {
            if (!sub.conn->connected())
                continue;
            sub.conn->send(env, drogon::WebSocketMessageType::Text);
            Metrics::counterInc("mes_ws_broadcast_delivered_total");
        }
    }
}

// ---- 实时 leader 选举 (计划任务 26): production.realtime 推送调度 ----
// Redis 键 ws:realtime:leader (PX 3000, 1Hz 续约); 多实例仅 leader 触发查询推送
const std::string kLeaderKey = "ws:realtime:leader";
const std::string kInstanceId = [] {
    std::random_device rd;
    std::ostringstream oss;
    oss << "mes-" << std::chrono::steady_clock::now().time_since_epoch().count() << "-" << rd();
    return oss.str();
}();

// 实时推送查询: 1Hz 查询进行中工单 (status=3) 并推送
// (多实例: 仅 leader 抢占成功后推送, 经 Redis Pub/Sub 分发到各实例)
// P4-5.4: LEFT JOIN prod_oee_stats 带出真 OEE 三因子 (当日聚合, 无数据时为 null)
void queryAndPushRealtime() {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT wo.work_order_no, wo.line_id, COALESCE(l.line_name,'') AS line_name, "
        "COALESCE(p.product_name,'') AS product_name, wo.plan_qty, wo.completed_qty, "
        "wo.good_qty, wo.defect_qty, wo.status, "
        "os.availability, os.performance, os.quality, os.oee "
        "FROM prod_work_orders wo "
        "LEFT JOIN prod_production_lines l ON l.id = wo.line_id "
        "LEFT JOIN prod_products p ON p.id = wo.product_id "
        "LEFT JOIN prod_oee_stats os ON os.line_id = wo.line_id AND os.stat_date = CURRENT_DATE "
        "WHERE wo.status = 3 ORDER BY wo.updated_at DESC LIMIT 20",
        [](const drogon::orm::Result& r) {
            for (const auto& row : r) {
                auto planQty = row["plan_qty"].isNull() ? 0 : row["plan_qty"].as<int>();
                auto goodQty = row["good_qty"].isNull() ? 0 : row["good_qty"].as<int>();
                auto completedQty =
                    row["completed_qty"].isNull() ? 0 : row["completed_qty"].as<int>();
                auto defectQty = row["defect_qty"].isNull() ? 0 : row["defect_qty"].as<int>();
                auto status = row["status"].isNull() ? 0 : row["status"].as<int>();
                nlohmann::json payload = {
                    {"line_id", row["line_id"].isNull() ? 0 : row["line_id"].as<int64_t>()},
                    {"line_name", row["line_name"].as<std::string>()},
                    {"work_order_no", row["work_order_no"].as<std::string>()},
                    {"product_name", row["product_name"].as<std::string>()},
                    {"target_qty", planQty},
                    {"completed_qty", completedQty},
                    {"good_qty", goodQty},
                    {"defect_qty", defectQty},
                    // 完工率 (yield_rate): good_qty/plan_qty*100
                    {"yield_rate", planQty > 0 ? goodQty * 100.0 / planQty : 0.0},
                    // 真 OEE 三因子 (P4-5.4, ISO 22400): 当日 prod_oee_stats 聚合,
                    // 无 run_status 传感器/无计划时为 null (大屏降级显示 yield_rate)
                    {"availability", row["availability"].isNull()
                                         ? nlohmann::json(nullptr)
                                         : nlohmann::json(row["availability"].as<double>())},
                    {"performance", row["performance"].isNull()
                                        ? nlohmann::json(nullptr)
                                        : nlohmann::json(row["performance"].as<double>())},
                    {"quality", row["quality"].isNull()
                                    ? nlohmann::json(nullptr)
                                    : nlohmann::json(row["quality"].as<double>())},
                    {"oee", row["oee"].isNull() ? nlohmann::json(nullptr)
                                                : nlohmann::json(row["oee"].as<double>())},
                    {"status", status},
                    {"timestamp", TimeUtils::nowUtcIso()}};
                publish("production.realtime", payload);
            }
        },
        [](const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "[ws] production.realtime query failed: " << e.base().what();
        });
}

// 1Hz tick: 尝试抢占 leader 锁, 抢占成功则推送, 使用 NX 保证仅 leader (多实例)
void publishProductionRealtime() {
    auto rdb = drogon::app().getRedisClient();
    rdb->execCommandAsync(
        [](const drogon::nosql::RedisResult& res) {
            bool mine = false;
            try {
                mine = res.type() == drogon::nosql::RedisResultType::kString &&
                       res.asString() == kInstanceId;
            } catch (...) {
            }
            auto rdb2 = drogon::app().getRedisClient();
            if (mine) {
                rdb2->execCommandAsync(
                    [](const drogon::nosql::RedisResult&) { queryAndPushRealtime(); },
                    [](const drogon::nosql::RedisException&) {}, "SET %s %s PX 3000 XX",
                    kLeaderKey.c_str(), kInstanceId.c_str());
            } else {
                rdb2->execCommandAsync(
                    [](const drogon::nosql::RedisResult& r2) {
                        try {
                            if (r2.type() == drogon::nosql::RedisResultType::kString &&
                                r2.asString() == "OK")
                                queryAndPushRealtime();
                        } catch (...) {
                        }
                    },
                    [](const drogon::nosql::RedisException&) {}, "SET %s %s NX PX 3000",
                    kLeaderKey.c_str(), kInstanceId.c_str());
            }
        },
        [](const drogon::nosql::RedisException&) {}, "GET %s", kLeaderKey.c_str());
}

// 本地合并入待发缓冲 (避免高频消息直接穿透 Redis 通道)
void publishLocal(const std::string& channel, const nlohmann::json& payload) {
    auto env = envelope(channel, payload);
    std::lock_guard lk(g_mu);
    g_pending[channel] = std::move(env); // 同频道仅保留最新 (合并)
}

// Redis Pub/Sub 订阅线程 (hiredis 阻塞读取; 断线 5s 重连)
// 注: 阻塞读取依赖 redisSetTimeout, Windows/hiredis 行为有差异;
// redisGetReply 超时返回 REDIS_ERR_TIMEOUT 时按断线处理 (重连),
// 需释放旧连接; 线程以 detach 方式运行 (随进程生命周期)
void redisSubscribeLoop(const std::string& host, int port) {
    while (!g_stop) {
        redisContext* ctx = redisConnectWithTimeout(host.c_str(), port, {5, 0});
        if (ctx == nullptr || ctx->err) {
            if (ctx) {
                LOG_WARN << "[ws] redis subscribe connect failed: " << ctx->errstr;
                redisFree(ctx);
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        std::string cmd = "SUBSCRIBE";
        for (const auto& ch : kChannels)
            cmd += " " + std::string(kRedisPrefix) + ch;
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, cmd.c_str()));
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
            LOG_WARN << "[ws] redis SUBSCRIBE failed";
            if (reply)
                freeReplyObject(reply);
            redisFree(ctx);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        freeReplyObject(reply);
        LOG_INFO << "[ws] redis subscriber connected (" << host << ":" << port << ")";

        while (!g_stop) {
            redisReply* msg = nullptr;
            int rc = redisGetReply(ctx, reinterpret_cast<void**>(&msg));
            if (rc != REDIS_OK) {
                LOG_WARN << "[ws] redisGetReply failed err=" << ctx->err << " "
                         << (ctx->errstr ? ctx->errstr : "");
                break; // 断线 -> 重连
            }
            if (msg && msg->type == REDIS_REPLY_ARRAY && msg->elements == 3 &&
                std::string(msg->element[0]->str) == "message") {
                std::string topic = msg->element[1]->str;
                std::string body = msg->element[2]->str;
                freeReplyObject(msg);
                if (topic.rfind(kRedisPrefix, 0) == 0) {
                    auto channel = topic.substr(std::string(kRedisPrefix).size());
                    try {
                        auto j = nlohmann::json::parse(body);
                        if (j.value("version", "") == "1.0" && j.value("channel", "") == channel &&
                            j.contains("payload")) {
                            // 标准信封 (与本地 publish() 一致): 直接合并入待发缓冲
                            std::lock_guard lk(g_mu);
                            g_pending[channel] = body;
                        } else {
                            // 非标准信封 (AlertHandler/DataIngestHandler 直发): 本地合并兜底
                            publishLocal(channel, j);
                        }
                    } catch (const std::exception& e) {
                        LOG_WARN << "[ws] broadcast payload parse failed: " << e.what();
                    }
                }
            } else if (msg) {
                freeReplyObject(msg);
            }
        }
        redisFree(ctx);
        if (!g_stop)
            std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

} // namespace

// 广播发布 (多实例, 计划任务 26): 经 Redis Pub/Sub 分发到各实例
// 各实例由 redisSubscribeLoop 订阅消费,
// 最终向本实例订阅连接推送 (跨实例广播)
void publish(const std::string& channel, const nlohmann::json& payload) {
    // 发布策略: 统一 v1.0 信封 (contracts/ws-push.schema.json);
    // 高频实时数据先本地合并, Redis 通道负责跨实例分发
    auto env = envelope(channel, payload);
    auto rdb = drogon::app().getRedisClient();
    auto ch = std::string(kRedisPrefix) + channel;
    Metrics::counterInc("mes_ws_broadcast_published_total");
    rdb->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                          [](const drogon::nosql::RedisException& e) {
                              LOG_WARN << "[ws] publish failed: " << e.what();
                          },
                          "PUBLISH %s %s", ch.c_str(), env.c_str());
}

void subscribe(const std::string& channel, const drogon::WebSocketConnectionPtr& conn) {
    std::lock_guard lk(g_mu);
    auto& vec = g_subs[channel];
    for (auto& s : vec)
        if (s.conn == conn)
            return;
    if (vec.size() >= kMaxSubsPerChannel) {
        LOG_WARN << "[ws] channel " << channel << " subscriber limit reached, drop oldest";
        vec.erase(vec.begin());
    }
    vec.push_back({conn});
    LOG_INFO << "[ws] subscribe channel=" << channel << " total=" << vec.size();
}

void unsubscribeAll(const drogon::WebSocketConnectionPtr& conn) {
    std::lock_guard lk(g_mu);
    for (auto& [channel, vec] : g_subs) {
        auto before = vec.size();
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [&conn](const Subscriber& s) { return s.conn == conn; }),
                  vec.end());
        if (vec.size() != before)
            LOG_INFO << "[ws] unsubscribe channel=" << channel << " total=" << vec.size();
    }
}

size_t subscriberCount(const std::string& channel) {
    std::lock_guard lk(g_mu);
    auto it = g_subs.find(channel);
    return it == g_subs.end() ? 0 : it->second.size();
}

void start() {
    g_stop = false;
    // Redis 地址取自 drogon custom_config (redis_host/redis_port), 默认本机
    std::string host = "127.0.0.1";
    int port = 6379;
    const auto& custom = drogon::app().getCustomConfig();
    if (custom.isMember("redis_host"))
        host = custom["redis_host"].asString();
    if (custom.isMember("redis_port"))
        port = custom["redis_port"].asInt();
    g_redisThread = std::thread([host, port] { redisSubscribeLoop(host, port); });
    g_redisThread.detach();
    // 启动合并 tick + production.realtime 实时推送
    drogon::app().getLoop()->runEvery(kMergeTickSec, drainPending);
    drogon::app().getLoop()->runEvery(1.0, publishProductionRealtime);
}

void stop() {
    g_stop = true;
}

} // namespace mes::WsBroadcastManager
