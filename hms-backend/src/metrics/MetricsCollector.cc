#include "metrics/MetricsCollector.hh"

#include <drogon/drogon.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "mq/MqProducer.hh"
#include "utils/Metrics.hh"
#include "websocket/WsBroadcastManager.hh"

namespace hms::MetricsCollector {

namespace {

std::atomic<bool> g_stop{false};
std::thread g_mqThread;

// outbox 待投递 (status=0 待发 + status=2 未超重试上限的失败件, 与投递器扫描条件一致)
void collectOutboxPending() {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT COUNT(*) FROM mq_outbox WHERE status = 0 OR (status = 2 AND retry_count < 5)",
        [](const drogon::orm::Result& r) {
            if (!r.empty())
                Metrics::gaugeSet("hms_outbox_pending", r[0][0].as<double>());
        },
        [](const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "[metrics] outbox pending query failed: " << e.base().what();
        });
}

// 分区剩余天数: iot_raw_data 按日分区 (partman premake), 最远已建分区日期 - 今天;
// 低于告警阈值 (alerts.yml 设 7 天) 即触发分区缺失风险告警
void collectPartitionDaysLeft() {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT (MAX(to_date(substring(c.relname from 15), 'YYYYMMDD')) - CURRENT_DATE)::int "
        "FROM pg_class c "
        "JOIN pg_inherits i ON i.inhrelid = c.oid "
        "JOIN pg_class p ON p.oid = i.inhparent "
        "WHERE p.relname = 'iot_raw_data' AND c.relname ~ '^iot_raw_data_p[0-9]{8}$'",
        [](const drogon::orm::Result& r) {
            if (!r.empty() && !r[0][0].isNull())
                Metrics::gaugeSet("hms_partition_days_left{table=\"iot_raw_data\"}",
                                  r[0][0].as<double>());
        },
        [](const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "[metrics] partition days query failed: " << e.base().what();
        });
}

// WS 订阅总数 (各频道汇总)
void collectWsSubscribers() {
    size_t total = 0;
    for (const char* ch : {"production.realtime", "device.status", "alert", "workorder.event"})
        total += WsBroadcastManager::subscriberCount(ch);
    Metrics::gaugeSet("hms_ws_subscribers", static_cast<double>(total));
}

// MQ 队列深度 (SimpleAmqpClient 阻塞 API, 独立线程 5s 轮询 passive declare)
void mqLagLoop() {
    const char* queues[] = {"iot.data.queue", "iot.alert.queue", "iot.retry.queue", "iot.dlq"};
    while (!g_stop) {
        for (const char* q : queues) {
            auto n = MqProducer::queueMessageCount(q);
            if (n >= 0)
                Metrics::gaugeSet(std::string("hms_mq_queue_messages{queue=\"") + q + "\"}",
                                  static_cast<double>(n));
        }
        for (int i = 0; i < 50 && !g_stop; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace

void start() {
    g_stop = false;
    auto* loop = drogon::app().getLoop();
    loop->runEvery(5.0, [] {
        collectOutboxPending();
        collectWsSubscribers();
    });
    loop->runEvery(60.0, collectPartitionDaysLeft);
    collectPartitionDaysLeft(); // 启动即采一次
    g_mqThread = std::thread(mqLagLoop);
    g_mqThread.detach();
    LOG_INFO << "metrics collector started";
}

void stop() {
    g_stop = true;
}

} // namespace hms::MetricsCollector
