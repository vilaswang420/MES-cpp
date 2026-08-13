#include "mq/AlertHandler.hh"

#include <SimpleAmqpClient/Channel.h>
#include <SimpleAmqpClient/Envelope.h>
#include <drogon/drogon.h>

#include <atomic>
#include <thread>

#include "common/SqlParam.hh"
#include "utils/TimeUtils.hh"

namespace hms::AlertHandler {

namespace {

std::atomic<bool> g_stop{false};
std::thread g_thread;

} // namespace

void start(const nlohmann::json& config) {
    auto url = config.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/%2F");
    auto queue = config.value("alert_queue", "iot.alert.queue");

    g_stop = false;
    g_thread = std::thread([url, queue] {
        AmqpClient::Channel::ptr_t channel;
        std::string tag;
        while (!g_stop) {
            try {
                if (!channel) {
                    channel = AmqpClient::Channel::CreateFromUri(url);
                    channel->DeclareQueue(queue, true /*passive*/);
                    tag = channel->BasicConsume(queue, "", false, false, false, 50);
                    LOG_INFO << "alert consumer connected (queue=" << queue << ")";
                }
                AmqpClient::Envelope::ptr_t env;
                if (channel->BasicConsumeMessage(tag, env, 1000) && env) {
                    // 解析告警消息 (DataIngestHandler 阈值判定产出)
                    try {
                        auto j = nlohmann::json::parse(env->Message()->Body());
                        auto deviceId = j.value("device_id", (int64_t)0);
                        auto sensorId = j.value("sensor_id", (int64_t)0);
                        auto type = j.value("alert_type", "ERROR");
                        auto level = j.value("alert_level", 2);
                        auto value = j.value("alert_value", 0.0);
                        auto threshold = j.value("threshold", 0.0);
                        auto message = j.value("message", "");

                        // 写 iot_alerts
                        auto db = drogon::app().getDbClient();
                        db->execSqlAsync(
                            "INSERT INTO iot_alerts (device_id, sensor_id, alert_type, "
                            "alert_level, alert_value, threshold, message) "
                            "VALUES ($1,$2,$3,$4,$5,$6,$7)",
                            [](const drogon::orm::Result&) {},
                            [](const drogon::orm::DrogonDbException& e) {
                                LOG_ERROR << "alert insert failed: " << e.base().what();
                            },
                            SqlArg(deviceId), SqlArg(sensorId), type, SqlArg(level),
                            SqlArg(value), SqlArg(threshold), message);

                        // Redis PUBLISH -> WS 广播频道 (M2 大屏弹窗)
                        // 载荷补齐大屏所需字段: level 语义映射 (3=critical 2=warning)
                        j["level"] = level >= 3 ? "critical" : "warning";
                        j["timestamp"] = TimeUtils::nowUtcIso();
                        auto rdb = drogon::app().getRedisClient();
                        auto payload = j.dump();
                        rdb->execCommandAsync([](const drogon::nosql::RedisResult&) {},
                                              [](const drogon::nosql::RedisException&) {},
                                              "PUBLISH ws:broadcast:alert %s", payload.c_str());
                    } catch (const std::exception& e) {
                        LOG_WARN << "alert message parse failed: " << e.what();
                    }
                    channel->BasicAck(env);
                }
            } catch (const std::exception& e) {
                LOG_WARN << "alert consumer unavailable: " << e.what();
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

} // namespace hms::AlertHandler
