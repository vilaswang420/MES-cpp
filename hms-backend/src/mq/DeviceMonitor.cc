#include "mq/DeviceMonitor.hh"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <string>

#include "common/SqlParam.hh"
#include "mq/OutboxDispatcher.hh"

namespace hms::DeviceMonitor {

namespace {

// 心跳超时阈值: 设备 60s 无上报判离线 (CORE_PLAN 2.9A)
constexpr int kOfflineSec = 60;
// 扫描周期: 每 10s 一轮
constexpr double kScanIntervalSec = 10.0;

std::atomic<bool> g_scanning{false}; // 防多实例/上轮未完成时重复扫描

// 单台设备: 原子置离线 + 写 OFFLINE 告警 outbox。
// 仅当 UPDATE 实际生效 (status 1->0, affectedRows=1) 才发告警,
// 多实例并发扫描时只有一台会命中, 天然去重。
void markOfflineAndAlert(int64_t deviceId, const std::string& deviceCode) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_devices SET status = 0, updated_at = NOW() "
        "WHERE id = $1 AND status = 1 AND deleted = FALSE",
        [deviceId, deviceCode](const drogon::orm::Result& r) {
            if (r.affectedRows() == 0)
                return; // 已被其他实例置离线, 不重复告警
            LOG_INFO << "[device-monitor] offline: device=" << deviceCode << " (id=" << deviceId
                     << ")";
            auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
            nlohmann::json alert = {{"version", "1.0"},
                                    {"device_id", deviceId},
                                    {"device_code", deviceCode},
                                    {"sensor_id", 0},
                                    {"alert_type", "OFFLINE"},
                                    {"alert_level", 2},
                                    {"alert_value", 0},
                                    {"threshold", 0},
                                    {"message", "device " + deviceCode + " heartbeat timeout > " +
                                                    std::to_string(kOfflineSec) + "s"},
                                    {"ts", ts}};
            // 全项目 MQ 唯一入口: 写 outbox, Dispatcher 投递 alert.{id} -> AlertHandler
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                OutboxService::kEnqueueSql, [deviceId](const drogon::orm::Result&) {},
                [deviceId](const drogon::orm::DrogonDbException& e) {
                    LOG_WARN << "[device-monitor] OFFLINE alert enqueue failed device=" << deviceId
                             << ": " << e.base().what();
                },
                "iot.exchange", "alert." + std::to_string(deviceId), alert.dump());
        },
        [deviceId](const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "[device-monitor] mark offline failed device=" << deviceId << ": "
                     << e.base().what();
        },
        SqlArg(deviceId));
}

void scan() {
    if (g_scanning.exchange(true))
        return;
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT id, device_code FROM iot_devices "
        "WHERE status = 1 AND deleted = FALSE "
        "AND last_heartbeat_at < NOW() - ($1::text)::interval",
        [](const drogon::orm::Result& r) {
            for (const auto& row : r)
                markOfflineAndAlert(row["id"].as<int64_t>(), row["device_code"].as<std::string>());
            g_scanning = false;
        },
        [](const drogon::orm::DrogonDbException& e) {
            g_scanning = false;
            LOG_WARN << "[device-monitor] scan failed: " << e.base().what();
        },
        std::to_string(kOfflineSec) + " seconds");
}

} // namespace

void start() {
    drogon::app().getLoop()->runEvery(kScanIntervalSec, [] { scan(); });
    LOG_INFO << "device monitor started (offline threshold " << kOfflineSec << "s, scan every "
             << kScanIntervalSec << "s)";
}

} // namespace hms::DeviceMonitor
