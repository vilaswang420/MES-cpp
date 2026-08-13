#include "websocket/WsBroadcastManager.hh"

#include <drogon/drogon.h>
#include <hiredis/hiredis.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "utils/TimeUtils.hh"

namespace hms::WsBroadcastManager {

namespace {

constexpr const char* kRedisPrefix = "ws:broadcast:";
constexpr double kMergeTickSec = 0.2;   // ??????? = 5Hz ??
// ?????: drogon send ?????, ??????? + trantor ????????;
// ???????????????????
constexpr size_t kMaxSubsPerChannel = 2000;

const std::vector<std::string> kChannels = {"production.realtime", "device.status", "alert",
                                            "workorder.event"};

struct Subscriber {
    drogon::WebSocketConnectionPtr conn;
};

std::mutex g_mu;
std::unordered_map<std::string, std::vector<Subscriber>> g_subs;
// ????: ??????????, tick ?????
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
        }
    }
}

// ?????????: 1Hz ?????? (status=3) ??
void publishProductionRealtime() {
    size_t subs = 0;
    {
        std::lock_guard lk(g_mu);
        auto it = g_subs.find("production.realtime");
        if (it != g_subs.end())
            subs = it->second.size();
    }
    if (subs == 0)
        return;
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT wo.work_order_no, wo.line_id, COALESCE(l.line_name,'') AS line_name, "
        "COALESCE(p.product_name,'') AS product_name, wo.plan_qty, wo.completed_qty, "
        "wo.good_qty, wo.defect_qty, wo.status "
        "FROM prod_work_orders wo "
        "LEFT JOIN prod_production_lines l ON l.id = wo.line_id "
        "LEFT JOIN prod_products p ON p.id = wo.product_id "
        "WHERE wo.status = 3 ORDER BY wo.updated_at DESC LIMIT 20",
        [](const drogon::orm::Result& r) {
            for (const auto& row : r) {
                auto planQty = row["plan_qty"].as<int>();
                auto goodQty = row["good_qty"].as<int>();
                nlohmann::json payload = {
                    {"line_id", row["line_id"].isNull() ? 0 : row["line_id"].as<int64_t>()},
                    {"line_name", row["line_name"].as<std::string>()},
                    {"work_order_no", row["work_order_no"].as<std::string>()},
                    {"product_name", row["product_name"].as<std::string>()},
                    {"target_qty", planQty},
                    {"completed_qty", row["completed_qty"].as<int>()},
                    {"good_qty", goodQty},
                    {"defect_qty", row["defect_qty"].as<int>()},
                    {"oee", planQty > 0 ? goodQty * 100.0 / planQty : 0.0},
                    {"status", row["status"].as<int>()},
                    {"timestamp", TimeUtils::nowUtcIso()}};
                publish("production.realtime", payload);
            }
        },
        [](const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "[ws] production.realtime query failed: " << e.base().what();
        });
}

// Redis Pub/Sub ???? (hiredis ????; ?? 5s ??)
// ?: ???????? redisSetTimeout ?? Windows/hiredis ????????
// redisGetReply ???? REDIS_ERR_TIMEOUT ???????? (??),
// ????????; ?????????? (detach ??)
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
                LOG_WARN << "[ws] redisGetReply failed err=" << ctx->err << " " <<
                    (ctx->errstr ? ctx->errstr : "");
                break; // ???? -> ??
            }
            if (msg && msg->type == REDIS_REPLY_ARRAY && msg->elements == 3 &&
                std::string(msg->element[0]->str) == "message") {
                std::string topic = msg->element[1]->str;
                std::string body = msg->element[2]->str;
                freeReplyObject(msg);
                if (topic.rfind(kRedisPrefix, 0) == 0) {
                    auto channel = topic.substr(std::string(kRedisPrefix).size());
                    try {
                        publish(channel, nlohmann::json::parse(body));
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

void publish(const std::string& channel, const nlohmann::json& payload) {
    auto env = envelope(channel, payload);
    std::lock_guard lk(g_mu);
    g_pending[channel] = std::move(env); // ???????? (????)
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
    // Redis ????? drogon redis_clients[0] ???? (custom_config ???)
    std::string host = "127.0.0.1";
    int port = 6379;
    const auto& custom = drogon::app().getCustomConfig();
    if (custom.isMember("redis_host"))
        host = custom["redis_host"].asString();
    if (custom.isMember("redis_port"))
        port = custom["redis_port"].asInt();
    g_redisThread = std::thread([host, port] { redisSubscribeLoop(host, port); });
    g_redisThread.detach();
    // ??????? tick + production.realtime ???
    drogon::app().getLoop()->runEvery(kMergeTickSec, drainPending);
    drogon::app().getLoop()->runEvery(1.0, publishProductionRealtime);
}

void stop() {
    g_stop = true;
}

} // namespace hms::WsBroadcastManager
