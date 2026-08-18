#include "services/OeeService.hh"

#include <SimpleAmqpClient/Channel.h>
#include <SimpleAmqpClient/Envelope.h>
#include <drogon/drogon.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "common/SqlParam.hh"
#include "utils/Metrics.hh"

// 真 OEE 消费者实现 (P4-5.4):
// 与 iot.data.queue (入库) 并行消费同一批 data.# 消息的复制 (oee.calc.queue, fan-out 复制,
// 不 ack 竞争、不影响入库链路)。消息本身不携带 OEE 语义 —— 消费仅作为"有新数据"信号,
// 限频触发当日全产线 OEE 重算 (SQL 聚合, 重启不丢状态, 可回填)。
namespace mes::OeeService {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kRecalcIntervalSec = 30;    // 重算限频
constexpr int64_t kRecalcAfterMsgs = 200; // 消息条数触发提前重算
constexpr double kMaxGapSec = 60.0;       // run_status 相邻点间隔上限 (断连不累计)

std::atomic<bool> g_stop{false};
std::thread g_thread;

// 当日 OEE 全量重算 (一条 SQL 聚合三因子原始量, C++ 计算因子, 逐行 UPSERT)
//
// A 因子: run_status 布尔传感器 (约定 sensor_code='run_status' 且 is_key_metric=TRUE)
//   每设备按相邻采集点间隔累计 (点值为 1 时累计 [prev, cur], 上限 60s 防断连虚高),
//   产线取瓶颈设备 MAX(run_seconds)。
// 计划生产时长: prod_production_plans 当日已确认/已执行的班次数 × 8h (28800s)。
// P 因子: Σ(prod_work_order_operations.completed_qty × process_steps.std_cycle_time)。
// Q 因子: qc_inspections 当日 SUM(pass_qty)/SUM(defect_qty) (按工单归属产线)。
void recalcAll() {
    static const char* kSql =
        "WITH dev_gap AS ("
        "  SELECT r.device_id, d.line_id, "
        "         LEAST(EXTRACT(EPOCH FROM (r.collected_at - "
        "             LAG(r.collected_at) OVER (PARTITION BY r.device_id ORDER BY "
        "r.collected_at))),"
        "             $1) AS gap_sec "
        "  FROM iot_raw_data r "
        "  JOIN iot_sensors s ON s.id = r.sensor_id AND s.is_key_metric = TRUE "
        "       AND s.sensor_code = 'run_status' AND s.deleted = FALSE "
        "  JOIN iot_devices d ON d.id = r.device_id "
        "  WHERE r.collected_at >= CURRENT_DATE AND r.value_num = 1 "
        "), dev_run AS ("
        "  SELECT device_id, line_id, SUM(gap_sec) AS run_sec "
        "  FROM dev_gap GROUP BY device_id, line_id"
        "), line_run AS ("
        "  SELECT line_id, MAX(run_sec) AS run_sec FROM dev_run GROUP BY line_id"
        "), plan AS ("
        "  SELECT line_id, shift, COUNT(*) * 28800 AS planned_sec "
        "  FROM prod_production_plans "
        "  WHERE plan_date = CURRENT_DATE AND status IN (1, 2) "
        "  GROUP BY line_id, shift"
        "), perf AS ("
        "  SELECT wo.line_id, SUM(o.completed_qty * COALESCE(ps.std_cycle_time, 0)) AS report_sec "
        "  FROM prod_work_order_operations o "
        "  JOIN prod_work_orders wo ON wo.id = o.work_order_id "
        "  LEFT JOIN prod_process_steps ps ON ps.id = o.process_step_id "
        "  WHERE wo.line_id IS NOT NULL AND wo.updated_at >= CURRENT_DATE "
        "  GROUP BY wo.line_id"
        "), qual AS ("
        "  SELECT wo.line_id, SUM(i.pass_qty) AS pass_qty, SUM(i.defect_qty) AS defect_qty "
        "  FROM qc_inspections i JOIN prod_work_orders wo ON wo.id = i.work_order_id "
        "  WHERE wo.line_id IS NOT NULL AND i.inspected_at >= CURRENT_DATE "
        "  GROUP BY wo.line_id"
        ") "
        "SELECT p.line_id, p.shift, COALESCE(lr.run_sec, 0), p.planned_sec, "
        "COALESCE(pf.report_sec, 0), COALESCE(q.pass_qty, 0), COALESCE(q.defect_qty, 0) "
        "FROM plan p "
        "LEFT JOIN line_run lr ON lr.line_id = p.line_id "
        "LEFT JOIN perf pf ON pf.line_id = p.line_id "
        "LEFT JOIN qual q ON q.line_id = p.line_id";

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        kSql,
        [](const drogon::orm::Result& r) {
            auto db2 = drogon::app().getDbClient();
            for (const auto& row : r) {
                OeeInput in;
                in.runSeconds = row[2].isNull() ? 0.0 : row[2].as<double>();
                in.plannedSeconds = row[3].isNull() ? 0.0 : row[3].as<double>();
                in.reportSeconds = row[4].isNull() ? 0.0 : row[4].as<double>();
                in.passQty = row[5].isNull() ? 0 : row[5].as<int64_t>();
                in.defectQty = row[6].isNull() ? 0 : row[6].as<int64_t>();
                auto res = computeOee(in);
                auto lineId = row[0].as<int64_t>();
                auto shift = row[1].as<int>();
                // UPSERT: 同 (line, date, shift) 覆盖更新, 因子值以本次重算为准
                db2->execSqlAsync(
                    "INSERT INTO prod_oee_stats (line_id, stat_date, shift, run_seconds, "
                    "planned_seconds, report_seconds, pass_qty, defect_qty, "
                    "availability, performance, quality, oee, updated_at) "
                    "VALUES ($1, CURRENT_DATE, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, NOW()) "
                    "ON CONFLICT (line_id, stat_date, shift) DO UPDATE SET "
                    "run_seconds = EXCLUDED.run_seconds, "
                    "planned_seconds = EXCLUDED.planned_seconds, "
                    "report_seconds = EXCLUDED.report_seconds, "
                    "pass_qty = EXCLUDED.pass_qty, defect_qty = EXCLUDED.defect_qty, "
                    "availability = EXCLUDED.availability, performance = EXCLUDED.performance, "
                    "quality = EXCLUDED.quality, oee = EXCLUDED.oee, updated_at = NOW()",
                    [lineId](const drogon::orm::Result&) {
                        Metrics::counterInc("mes_oee_recalc_rows_total");
                    },
                    [lineId](const drogon::orm::DrogonDbException& e) {
                        LOG_WARN << "[oee] upsert line " << lineId
                                 << " failed: " << e.base().what();
                    },
                    SqlArg(lineId), SqlArg(shift), SqlArg(in.runSeconds), SqlArg(in.plannedSeconds),
                    SqlArg(in.reportSeconds), SqlArg(in.passQty), SqlArg(in.defectQty),
                    SqlArg(res.availability * 100.0), SqlArg(res.performance * 100.0),
                    SqlArg(res.quality * 100.0), SqlArg(res.oee * 100.0));
            }
            if (!r.empty())
                LOG_DEBUG << "[oee] recalculated " << r.size() << " line-shift rows";
        },
        [](const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "[oee] recalc aggregate failed: " << e.base().what();
        },
        SqlArg(kMaxGapSec));
}

} // namespace

void start(const nlohmann::json& config) {
    auto url = config.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/%2F");
    auto queue = config.value("oee_queue", "oee.calc.queue");

    g_stop = false;
    g_thread = std::thread([url, queue] {
        AmqpClient::Channel::ptr_t channel;
        std::string tag;
        auto lastCalc = Clock::now() - std::chrono::seconds(kRecalcIntervalSec); // 启动即先算一次
        int64_t sinceCalc = 0;

        // 启动先重算一次 (不依赖 MQ 消息, 保证重启/补数场景 OEE 可用)
        recalcAll();
        lastCalc = Clock::now();

        while (!g_stop) {
            try {
                if (!channel) {
                    channel = AmqpClient::Channel::CreateFromUri(url);
                    channel->DeclareQueue(queue, true /*passive*/);
                    // prefetch 500: 仅作触发信号, 大预取降低 broker 压力
                    tag = channel->BasicConsume(queue, "", false, false, false, 500);
                    LOG_INFO << "oee consumer connected (queue=" << queue << ")";
                }
                AmqpClient::Envelope::ptr_t env;
                bool got = channel->BasicConsumeMessage(tag, env, 5);
                if (got && env) {
                    channel->BasicAck(env); // 消息即触发信号, 立即 ack (重算幂等)
                    ++sinceCalc;
                }
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - lastCalc)
                        .count();
                if (elapsed >= kRecalcIntervalSec || (got && sinceCalc >= kRecalcAfterMsgs)) {
                    recalcAll();
                    lastCalc = Clock::now();
                    sinceCalc = 0;
                }
            } catch (const std::exception& e) {
                LOG_WARN << "oee consumer unavailable: " << e.what();
                channel.reset();
                tag.clear();
                // 断连期间仍按限频重算 (数据可能仍在入库)
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - lastCalc)
                        .count();
                if (elapsed >= kRecalcIntervalSec) {
                    recalcAll();
                    lastCalc = Clock::now();
                }
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    });
    g_thread.detach();
}

void stop() {
    g_stop = true;
}

} // namespace mes::OeeService
