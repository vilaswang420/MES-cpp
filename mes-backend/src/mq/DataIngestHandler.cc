#include "mq/DataIngestHandler.hh"

#include <SimpleAmqpClient/BasicMessage.h>
#include <SimpleAmqpClient/Channel.h>
#include <SimpleAmqpClient/Envelope.h>
#include <SimpleAmqpClient/Table.h>
#include <drogon/drogon.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/SqlParam.hh"

namespace mes::DataIngestHandler {

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_stop{false};
std::thread g_thread;

constexpr int kMaxRetry = 3;          // 7.3 节: x-retry-count 达 3 后拒绝进 DLQ
constexpr size_t kBatchRows = 500;    // 计划任务 19: 每批 500 条
constexpr int kFlushWindowMs = 100;   // 或 100ms 窗口 flush
constexpr int kAlertSuppressSec = 30; // 同传感器同向告警抑制窗口

struct Row {
    int64_t deviceId;
    std::string deviceCode; // device.status 广播载荷用
    int64_t sensorId;
    bool isNum;
    double num;
    std::string str;
    int quality;
    std::string ts;
};

// ack 队列: drogon DB 回调在其 IO 线程触发, 而 SimpleAmqpClient channel 非线程安全,
// 故回调仅登记结果, 由消费者线程统一 ack/reject
std::mutex g_ackMtx;
std::deque<std::pair<AmqpClient::Envelope::DeliveryInfo, bool>> g_ackQueue;

void enqueueAck(const AmqpClient::Envelope::DeliveryInfo& info, bool ok) {
    std::lock_guard lk(g_ackMtx);
    g_ackQueue.emplace_back(info, ok);
}

// ---- 传感器阈值缓存 (告警判定用, 60s 异步刷新) ----
struct SensorCfg {
    bool hasLow = false, hasHigh = false, hasLowLow = false, hasHighHigh = false;
    double low = 0, high = 0, lowLow = 0, highHigh = 0;
};
using SensorMap = std::unordered_map<int64_t, SensorCfg>; // key = deviceId*1000000+sensorId
std::mutex g_sensorMtx;
std::shared_ptr<const SensorMap> g_sensors; // 不可变快照, 原子替换
Clock::time_point g_sensorLoadAt{};
std::atomic<bool> g_sensorLoading{false};

int64_t sensorKey(int64_t deviceId, int64_t sensorId) {
    return deviceId * 1000000 + sensorId;
}

void refreshSensorCache() {
    if (g_sensorLoading.exchange(true))
        return;
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT device_id, id AS sensor_id, alarm_low, alarm_high, alarm_low_low, "
        "alarm_high_high FROM iot_sensors WHERE status = 1 AND deleted = FALSE",
        [](const drogon::orm::Result& r) {
            auto m = std::make_shared<SensorMap>();
            for (const auto& row : r) {
                SensorCfg c;
                if (!row["alarm_low"].isNull()) {
                    c.hasLow = true;
                    c.low = row["alarm_low"].as<double>();
                }
                if (!row["alarm_high"].isNull()) {
                    c.hasHigh = true;
                    c.high = row["alarm_high"].as<double>();
                }
                if (!row["alarm_low_low"].isNull()) {
                    c.hasLowLow = true;
                    c.lowLow = row["alarm_low_low"].as<double>();
                }
                if (!row["alarm_high_high"].isNull()) {
                    c.hasHighHigh = true;
                    c.highHigh = row["alarm_high_high"].as<double>();
                }
                (*m)[sensorKey(row["device_id"].as<int64_t>(), row["sensor_id"].as<int64_t>())] = c;
            }
            {
                std::lock_guard lk(g_sensorMtx);
                g_sensors = m;
                g_sensorLoadAt = Clock::now();
            }
            g_sensorLoading = false;
            LOG_INFO << "sensor threshold cache loaded: " << m->size() << " sensors";
        },
        [](const drogon::orm::DrogonDbException& e) {
            g_sensorLoading = false;
            LOG_WARN << "sensor cache load failed: " << e.base().what();
        });
}

std::shared_ptr<const SensorMap> sensorSnapshot() {
    {
        std::lock_guard lk(g_sensorMtx);
        if (g_sensors && Clock::now() - g_sensorLoadAt < std::chrono::seconds(60))
            return g_sensors;
    }
    refreshSensorCache(); // 过期触发异步刷新, 本次仍用旧快照 (或空)
    std::lock_guard lk(g_sensorMtx);
    return g_sensors;
}

// 告警抑制: 同传感器同方向 30s 内不重复发布
std::unordered_map<int64_t, Clock::time_point> g_lastAlert; // key = sensorKey*8+方向位

// ---- schema 校验 (contracts/iot-message.schema.json required 字段与类型) ----
bool validate(const nlohmann::json& j, std::string& err) {
    if (!j.is_object()) {
        err = "payload 不是 JSON 对象";
        return false;
    }
    if (!j.contains("version") || !j["version"].is_string()) {
        err = "version 缺失或非字符串";
        return false;
    }
    if (!j.contains("device_id") || !j["device_id"].is_number_integer()) {
        err = "device_id 缺失或非整数";
        return false;
    }
    if (!j.contains("device_code") || !j["device_code"].is_string()) {
        err = "device_code 缺失或非字符串";
        return false;
    }
    if (!j.contains("sensor_id") || !j["sensor_id"].is_number_integer()) {
        err = "sensor_id 缺失或非整数";
        return false;
    }
    if (!j.contains("value") || (!j["value"].is_number() && !j["value"].is_string())) {
        err = "value 缺失或类型非法";
        return false;
    }
    if (!j.contains("quality") || !j["quality"].is_number_integer()) {
        err = "quality 缺失或非整数";
        return false;
    }
    if (!j.contains("ts") || !j["ts"].is_string()) {
        err = "ts 缺失或非字符串";
        return false;
    }
    return true;
}

// ts 必须为受控 ISO8601 字符集 (数字与 -T:.Z), 供安全内联/绑定
bool tsSafe(const std::string& ts) {
    if (ts.size() < 19 || ts.size() > 32)
        return false;
    for (char c : ts)
        if (!(isdigit(static_cast<unsigned char>(c)) || c == '-' || c == 'T' || c == ':' ||
              c == '.' || c == 'Z'))
            return false;
    return true;
}

int retryCountOf(const AmqpClient::BasicMessage::ptr_t& msg) {
    if (!msg->HeaderTableIsSet())
        return 0;
    const auto& t = msg->HeaderTable();
    auto it = t.find("x-retry-count");
    if (it == t.end())
        return 0;
    return static_cast<int>(it->second.GetInteger());
}

// 有界重试: 递增 x-retry-count 重发延迟重试队列 (TTL 10s 后 DLX 回 data.# 业务队列);
// 达上限则拒绝不重入队, 由队列 DLX 参数路由进 iot.dlq
void handleInvalid(const AmqpClient::Channel::ptr_t& channel, const std::string& exchange,
                   const AmqpClient::Envelope::ptr_t& env, const std::string& err) {
    auto msg = env->Message();
    int retry = retryCountOf(msg);
    if (retry >= kMaxRetry) {
        LOG_WARN << "[ingest] message exhausted retries -> DLQ: " << err;
        channel->BasicReject(env, false);
        return;
    }
    auto repub = AmqpClient::BasicMessage::Create(msg->Body());
    repub->ContentType("application/json");
    repub->DeliveryMode(AmqpClient::BasicMessage::delivery_mode_t::dm_persistent);
    auto table = msg->HeaderTableIsSet() ? msg->HeaderTable() : AmqpClient::Table{};
    table["x-retry-count"] = AmqpClient::TableValue(static_cast<int64_t>(retry + 1));
    repub->HeaderTable(table);
    channel->BasicPublish(exchange, "retry.data", repub);
    channel->BasicAck(env);
    LOG_INFO << "[ingest] invalid message retry " << retry + 1 << "/" << kMaxRetry << ": " << err;
}

// 阈值判定 -> 发布 alert.{device_id} (AlertHandler 落库+推送)
void checkThreshold(const AmqpClient::Channel::ptr_t& channel, const std::string& exchange,
                    const Row& row) {
    auto sensors = sensorSnapshot();
    if (!sensors)
        return;
    auto it = sensors->find(sensorKey(row.deviceId, row.sensorId));
    if (it == sensors->end() || !row.isNum)
        return;
    const auto& c = it->second;
    std::string type;
    double threshold = 0;
    int level = 2;
    if (c.hasHighHigh && row.num >= c.highHigh) {
        type = "HIGH_HIGH";
        threshold = c.highHigh;
        level = 3;
    } else if (c.hasHigh && row.num >= c.high) {
        type = "HIGH";
        threshold = c.high;
    } else if (c.hasLowLow && row.num <= c.lowLow) {
        type = "LOW_LOW";
        threshold = c.lowLow;
        level = 3;
    } else if (c.hasLow && row.num <= c.low) {
        type = "LOW";
        threshold = c.low;
    }
    if (type.empty())
        return;

    // 方向位: HIGH系=0 LOW系=1
    int dir = (type.rfind("LOW", 0) == 0) ? 1 : 0;
    int64_t dedupKey = sensorKey(row.deviceId, row.sensorId) * 8 + dir * 4 + (level - 2);
    auto now = Clock::now();
    auto last = g_lastAlert.find(dedupKey);
    if (last != g_lastAlert.end() && now - last->second < std::chrono::seconds(kAlertSuppressSec))
        return;
    g_lastAlert[dedupKey] = now;

    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    nlohmann::json alert = {{"version", "1.0"},
                            {"device_id", row.deviceId},
                            {"device_code", row.deviceCode},
                            {"sensor_id", row.sensorId},
                            {"alert_type", type},
                            {"alert_level", level},
                            {"alert_value", row.num},
                            {"threshold", threshold},
                            {"message", "sensor " + std::to_string(row.sensorId) + " value " +
                                            std::to_string(row.num) + " crossed " + type},
                            {"ts", ts}};
    auto alertMsg = AmqpClient::BasicMessage::Create(alert.dump());
    alertMsg->ContentType("application/json");
    alertMsg->DeliveryMode(AmqpClient::BasicMessage::delivery_mode_t::dm_persistent);
    channel->BasicPublish(exchange, "alert." + std::to_string(row.deviceId), alertMsg);
}

// 多行 VALUES 批量写 iot_raw_data (参数化, 一条语句一批)
void flushBatch(std::vector<Row>& rows, std::vector<AmqpClient::Envelope::DeliveryInfo>& dels) {
    if (rows.empty())
        return;
    auto count = rows.size();
    std::string sql = "INSERT INTO iot_raw_data (device_id, sensor_id, value_num, value_str, "
                      "quality, collected_at) VALUES ";
    for (size_t i = 0; i < count; ++i) {
        auto base = i * 6 + 1;
        sql += "($";
        sql += std::to_string(base) + ",$" + std::to_string(base + 1) + ",$" +
               std::to_string(base + 2) + ",$" + std::to_string(base + 3) + ",$" +
               std::to_string(base + 4) + ",$" + std::to_string(base + 5) + ")";
        if (i + 1 < count)
            sql += ",";
    }

    auto db = drogon::app().getDbClient();
    auto binder = *db << sql;
    for (const auto& r : rows) {
        binder << SqlArg(r.deviceId) << SqlArg(r.sensorId);
        if (r.isNum)
            binder << SqlArg(r.num) << SqlArgNull();
        else
            binder << SqlArgNull() << r.str;
        binder << SqlArg(r.quality) << r.ts;
    }
    auto deliveries = std::move(dels);
    std::vector<std::pair<int64_t, Row>> latest; // device 最新值 (Redis 更新)
    latest.reserve(count);
    for (const auto& r : rows)
        latest.emplace_back(r.deviceId, r);
    binder >> [deliveries, latest, count](const drogon::orm::Result&) {
        for (const auto& d : deliveries)
            enqueueAck(d, true);
        // device:latest:{deviceId} 并行拆分更新 (计划任务 19)
        auto rdb = drogon::app().getRedisClient();
        std::unordered_map<int64_t, size_t> seen;
        for (size_t i = 0; i < latest.size(); ++i)
            seen[latest[i].first] = i; // 同设备取批内最后一条
        // P1-2.9A 心跳: 批次内 distinct 设备一条 UPDATE 刷新 last_heartbeat_at 并置在线
        // (恢复上报即重新在线; 离线判定见 DeviceMonitor)
        std::string ids = "{";
        bool firstId = true;
        for (const auto& [devId, idx] : seen) {
            if (!firstId)
                ids += ",";
            firstId = false;
            ids += std::to_string(devId);
            const auto& r = latest[idx].second;
            nlohmann::json v = {{"device_id", r.deviceId},
                                {"device_code", r.deviceCode},
                                {"sensor_id", r.sensorId},
                                {"value", r.isNum ? nlohmann::json(r.num) : nlohmann::json(r.str)},
                                {"quality", r.quality},
                                {"ts", r.ts}};
            auto key = "device:latest:" + std::to_string(devId);
            auto payload = v.dump();
            rdb->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                                  [](const drogon::nosql::RedisException&) {}, "SET %s %s EX 86400",
                                  key.c_str(), payload.c_str());
            // device.status 广播 -> WsBroadcastManager (Redis 订阅线程转发到大屏)
            rdb->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                                  [](const drogon::nosql::RedisException&) {},
                                  "PUBLISH ws:broadcast:device.status %s", payload.c_str());
        }
        ids += "}";
        auto hbDb = drogon::app().getDbClient();
        hbDb->execSqlAsync(
            "UPDATE iot_devices SET last_heartbeat_at = NOW(), status = 1 "
            "WHERE id = ANY($1::bigint[]) AND deleted = FALSE",
            [](const drogon::orm::Result&) {},
            [](const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "[ingest] heartbeat update failed: " << e.base().what();
            },
            ids);
        static std::atomic<int64_t> ingested{0};
        auto total = ingested.fetch_add(static_cast<int64_t>(count)) + static_cast<int64_t>(count);
        if (total % 5000 < static_cast<int64_t>(count))
            LOG_INFO << "[ingest] ingested total: " << total;
    };
    binder >> [deliveries](const drogon::orm::DrogonDbException& e) {
        // DB 失败: 整批重入队重试 (分区缺失等持久错误由日志暴露)
        LOG_ERROR << "[ingest] batch insert failed (" << deliveries.size()
                  << " msgs requeued): " << e.base().what();
        for (const auto& d : deliveries)
            enqueueAck(d, false);
    };
    rows.clear();
    dels.clear();
}

void handleMessage(const AmqpClient::Channel::ptr_t& channel, const std::string& exchange,
                   const AmqpClient::Envelope::ptr_t& env, std::vector<Row>& rows,
                   std::vector<AmqpClient::Envelope::DeliveryInfo>& dels) {
    nlohmann::json j;
    std::string err;
    try {
        j = nlohmann::json::parse(env->Message()->Body());
    } catch (const std::exception& e) {
        err = std::string("JSON 解析失败: ") + e.what();
    }
    if (err.empty() && !validate(j, err)) {
    }
    if (!err.empty()) {
        handleInvalid(channel, exchange, env, err);
        return;
    }
    auto ts = j["ts"].get<std::string>();
    if (!tsSafe(ts)) {
        handleInvalid(channel, exchange, env, "ts 格式非法");
        return;
    }
    Row row;
    row.deviceId = j["device_id"].get<int64_t>();
    row.deviceCode = j["device_code"].get<std::string>();
    row.sensorId = j["sensor_id"].get<int64_t>();
    if (j["value"].is_number()) {
        row.isNum = true;
        row.num = j["value"].get<double>();
    } else {
        row.isNum = false;
        row.str = j["value"].get<std::string>().substr(0, 256);
    }
    row.quality = j["quality"].get<int>();
    row.ts = std::move(ts);

    checkThreshold(channel, exchange, row);
    rows.push_back(std::move(row));
    dels.push_back(env->GetDeliveryInfo());
}

} // namespace

void start(const nlohmann::json& config) {
    auto url = config.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/%2F");
    auto exchange = config.value("exchange", "iot.exchange");
    auto queue = config.value("data_queue", "iot.data.queue");
    auto prefetch = config.value("prefetch_count", 50);

    g_stop = false;
    g_thread = std::thread([url, exchange, queue, prefetch] {
        AmqpClient::Channel::ptr_t channel;
        std::string tag;
        std::vector<Row> rows;
        std::vector<AmqpClient::Envelope::DeliveryInfo> dels;
        rows.reserve(kBatchRows);
        auto lastFlush = Clock::now();

        refreshSensorCache();
        while (!g_stop) {
            try {
                if (!channel) {
                    channel = AmqpClient::Channel::CreateFromUri(url);
                    channel->DeclareQueue(queue, true /*passive*/);
                    // no_ack=false 手动 ack; prefetch 起步 50 (计划任务 19 压测调至 200-500)
                    tag = channel->BasicConsume(queue, "", false, false, false, prefetch);
                    LOG_INFO << "data-ingest consumer connected (queue=" << queue
                             << ", prefetch=" << prefetch << ")";
                }
                // 先处理 DB 回调登记的 ack/reject (channel 非线程安全, 必须本线程执行)
                {
                    std::deque<std::pair<AmqpClient::Envelope::DeliveryInfo, bool>> acks;
                    {
                        std::lock_guard lk(g_ackMtx);
                        acks.swap(g_ackQueue);
                    }
                    for (auto& [info, ok] : acks) {
                        if (ok)
                            channel->BasicAck(info);
                        else
                            channel->BasicReject(info, true);
                    }
                }
                AmqpClient::Envelope::ptr_t env;
                bool got = channel->BasicConsumeMessage(tag, env, 20);
                if (got && env) {
                    handleMessage(channel, exchange, env, rows, dels);
                    lastFlush = Clock::now();
                }
                bool windowElapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastFlush)
                        .count() >= kFlushWindowMs;
                if (!rows.empty() && (rows.size() >= kBatchRows || (windowElapsed && !got))) {
                    flushBatch(rows, dels);
                    lastFlush = Clock::now();
                }
            } catch (const std::exception& e) {
                LOG_WARN << "data-ingest consumer unavailable: " << e.what();
                channel.reset();
                tag.clear();
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    });
    g_thread.detach();
}

void stop() {
    g_stop = true;
}

} // namespace mes::DataIngestHandler
