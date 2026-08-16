#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>

// 真实 OEE 计算消费者 (P4-5.4 / ISO 22400):
//   OEE = A(可用率) × P(表现性) × Q(质量率)
//   A = 运行时间 / 计划生产时间   (数据源: run_status 布尔传感器累计, 见 OeeService.cc)
//   P = Σ(报工量 × 理想节拍) / 运行时间
//   Q = 合格品 / 总产出 (qc_inspections pass/(pass+defect))
// 聚合粒度 (line_id, stat_date, shift) → prod_oee_stats 表;
// WsBroadcastManager 1Hz 读取推送 production.realtime (availability/performance/quality/oee)。
//
// 纯计算部分 (computeOee) header-only 且不依赖 Drogon, 由 tests/test_oee_calc.cc 覆盖;
// MQ 消费与 SQL 聚合在 OeeService.cc (复用 DataIngestHandler 的消费线程模式)。
namespace hms::OeeService {

// ---- 纯计算层 (可单测) ----

struct OeeInput {
    double runSeconds = 0;     // A: 运行累计秒
    double plannedSeconds = 0; // A: 计划生产秒
    double reportSeconds = 0;  // P: Σ(报工量 × 节拍) 秒
    int64_t passQty = 0;       // Q: 合格数
    int64_t defectQty = 0;     // Q: 缺陷数
};

struct OeeResult {
    double availability = 0; // 0-1
    double performance = 0;  // 0-1
    double quality = 0;      // 0-1
    double oee = 0;          // 0-1 (A×P×Q)
};

inline double clamp01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

// 三因子计算:
// - 计划/运行时间缺失 (<=0) 时 A=0 (无计划即无 OEE, 不虚报)
// - P 上限 1.0 (超产/超速按 100% 封顶, 保证 OEE 0-100 合理区间)
// - Q 无质检数据 (pass+defect=0) 时按 1.0 计 (无不良记录视为全好, 避免无质检即 OEE=0)
inline OeeResult computeOee(const OeeInput& in) {
    OeeResult r;
    r.availability =
        (in.plannedSeconds > 0 && in.runSeconds > 0) ? clamp01(in.runSeconds / in.plannedSeconds)
                                                     : 0.0;
    r.performance = (in.runSeconds > 0 && in.reportSeconds > 0)
                        ? clamp01(in.reportSeconds / in.runSeconds)
                        : 0.0;
    r.quality = (in.passQty + in.defectQty) > 0
                    ? clamp01(static_cast<double>(in.passQty) /
                              static_cast<double>(in.passQty + in.defectQty))
                    : 1.0;
    r.oee = r.availability * r.performance * r.quality;
    return r;
}

// ---- 消费者层 (OeeService.cc 实现) ----

// 启动 MQ 消费者: 消费 oee.calc.queue (iot.exchange data.# fan-out 复制),
// 限频 (30s 或 200 条) 触发当日全产线 OEE 重算并 UPSERT prod_oee_stats。
// config: { amqp_url, oee_queue (默认 oee.calc.queue), recalc_interval_ms }
void start(const nlohmann::json& config);
void stop();

} // namespace hms::OeeService
