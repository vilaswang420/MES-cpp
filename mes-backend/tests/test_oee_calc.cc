// P4-5.4 真 OEE 三因子计算单测 (纯计算层, 不依赖 Drogon/DB)
// 口径: ISO 22400 — OEE = A(可用率) × P(表现性) × Q(质量率)
#include <gtest/gtest.h>

#include "services/OeeService.hh"

using mes::OeeService::computeOee;
using mes::OeeService::OeeInput;

// 全满负荷 + 全合格: OEE = 100%
TEST(OeeCalc, PerfectLine) {
    OeeInput in{.runSeconds = 28800,
                .plannedSeconds = 28800,
                .reportSeconds = 28800,
                .passQty = 100,
                .defectQty = 0};
    auto r = computeOee(in);
    EXPECT_DOUBLE_EQ(r.availability, 1.0);
    EXPECT_DOUBLE_EQ(r.performance, 1.0);
    EXPECT_DOUBLE_EQ(r.quality, 1.0);
    EXPECT_DOUBLE_EQ(r.oee, 1.0);
}

// 经典示例: A=90% P=95% Q=99.9% -> OEE≈85.4%
TEST(OeeCalc, ClassicFactors) {
    OeeInput in{.runSeconds = 25920,
                .plannedSeconds = 28800,
                .reportSeconds = 24624,
                .passQty = 999,
                .defectQty = 1};
    auto r = computeOee(in);
    EXPECT_NEAR(r.availability, 0.9, 1e-9);
    EXPECT_NEAR(r.performance, 0.95, 1e-9);
    EXPECT_NEAR(r.quality, 0.999, 1e-9);
    EXPECT_NEAR(r.oee, 0.9 * 0.95 * 0.999, 1e-9);
}

// 无计划生产时间 (当日无排产): A=0 -> OEE=0 (无计划不虚报)
TEST(OeeCalc, NoPlanZeroOee) {
    OeeInput in{.runSeconds = 3600,
                .plannedSeconds = 0,
                .reportSeconds = 3600,
                .passQty = 100,
                .defectQty = 0};
    auto r = computeOee(in);
    EXPECT_DOUBLE_EQ(r.availability, 0.0);
    EXPECT_DOUBLE_EQ(r.oee, 0.0);
}

// 无运行数据但有计划: 同样 OEE=0
TEST(OeeCalc, NoRunData) {
    OeeInput in{.runSeconds = 0,
                .plannedSeconds = 28800,
                .reportSeconds = 0,
                .passQty = 50,
                .defectQty = 50};
    auto r = computeOee(in);
    EXPECT_DOUBLE_EQ(r.availability, 0.0);
    EXPECT_DOUBLE_EQ(r.performance, 0.0);
    EXPECT_DOUBLE_EQ(r.oee, 0.0);
}

// P 超 1 (超产/超速) 封顶为 1: 保证 OEE 落在 0-100 合理区间
TEST(OeeCalc, PerformanceCappedAtOne) {
    OeeInput in{.runSeconds = 10000,
                .plannedSeconds = 28800,
                .reportSeconds = 15000,
                .passQty = 100,
                .defectQty = 0};
    auto r = computeOee(in);
    EXPECT_DOUBLE_EQ(r.performance, 1.0);
    EXPECT_DOUBLE_EQ(r.oee, r.availability * 1.0 * 1.0);
    EXPECT_LE(r.oee, 1.0);
}

// A 超 1 (数据毛刺/重复累计) 同样封顶
TEST(OeeCalc, AvailabilityCappedAtOne) {
    OeeInput in{.runSeconds = 30000,
                .plannedSeconds = 28800,
                .reportSeconds = 10000,
                .passQty = 100,
                .defectQty = 0};
    auto r = computeOee(in);
    EXPECT_DOUBLE_EQ(r.availability, 1.0);
}

// 无质检记录 (pass+defect=0): Q 按 1.0 计 (无不良记录视为全好)
TEST(OeeCalc, NoQualityDataDefaultsFull) {
    OeeInput in{.runSeconds = 28800,
                .plannedSeconds = 28800,
                .reportSeconds = 28800,
                .passQty = 0,
                .defectQty = 0};
    auto r = computeOee(in);
    EXPECT_DOUBLE_EQ(r.quality, 1.0);
    EXPECT_NEAR(r.oee, 1.0, 1e-9);
}

// Q 因子: 80% 合格率
TEST(OeeCalc, QualityRatio) {
    OeeInput in{.runSeconds = 28800,
                .plannedSeconds = 28800,
                .reportSeconds = 28800,
                .passQty = 800,
                .defectQty = 200};
    auto r = computeOee(in);
    EXPECT_NEAR(r.quality, 0.8, 1e-9);
    EXPECT_NEAR(r.oee, 0.8, 1e-9);
}

// 负值输入防御: 一律归 0
TEST(OeeCalc, NegativeInputClamped) {
    OeeInput in{.runSeconds = -100,
                .plannedSeconds = 28800,
                .reportSeconds = -5,
                .passQty = -1,
                .defectQty = 0};
    auto r = computeOee(in);
    EXPECT_DOUBLE_EQ(r.availability, 0.0);
    EXPECT_DOUBLE_EQ(r.performance, 0.0);
    EXPECT_DOUBLE_EQ(r.oee, 0.0);
}

// 与手工计算比对 (方案验收: 抽样 3 条)
// 手工: A = 7.5h/8h = 0.9375, P = 6.75h/7.5h = 0.9, Q = 950/1000 = 0.95
// OEE = 0.9375 × 0.9 × 0.95 = 0.8015625
TEST(OeeCalc, ManualVerificationSample1) {
    OeeInput in{.runSeconds = 27000,
                .plannedSeconds = 28800,
                .reportSeconds = 24300,
                .passQty = 950,
                .defectQty = 50};
    auto r = computeOee(in);
    EXPECT_NEAR(r.availability, 0.9375, 1e-9);
    EXPECT_NEAR(r.performance, 0.9, 1e-9);
    EXPECT_NEAR(r.quality, 0.95, 1e-9);
    EXPECT_NEAR(r.oee, 0.8015625, 1e-9);
}

// 手工: A = 4h/8h = 0.5, P = 3h/4h = 0.75, Q = 1.0 (无缺陷)
// OEE = 0.375
TEST(OeeCalc, ManualVerificationSample2) {
    OeeInput in{.runSeconds = 14400,
                .plannedSeconds = 28800,
                .reportSeconds = 10800,
                .passQty = 500,
                .defectQty = 0};
    auto r = computeOee(in);
    EXPECT_NEAR(r.oee, 0.375, 1e-9);
}

// 手工: A = 1.0, P = 0.6, Q = 0.9 -> OEE = 0.54
TEST(OeeCalc, ManualVerificationSample3) {
    OeeInput in{.runSeconds = 28800,
                .plannedSeconds = 28800,
                .reportSeconds = 17280,
                .passQty = 900,
                .defectQty = 100};
    auto r = computeOee(in);
    EXPECT_NEAR(r.performance, 0.6, 1e-9);
    EXPECT_NEAR(r.quality, 0.9, 1e-9);
    EXPECT_NEAR(r.oee, 0.54, 1e-9);
}
