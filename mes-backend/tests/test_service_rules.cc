#include <gtest/gtest.h>

#include "utils/IntegRules.hh"
#include "utils/QcRules.hh"
#include "utils/ReportRules.hh"

// P2-3.2 Service 层关键规则单测 (纯逻辑, 与 Service 实现共用同一规则头):
// WorkOrderService 报工规则 / QcService 直通率+处置 / IntegrationService 解析+过滤哨兵。
using namespace mes::ReportRules;
using namespace mes::QcRules;
using namespace mes::IntegRules;

// ==================== ReportRules (报工) ====================

TEST(ReportRules, StepSeqMustBePositive) {
    EXPECT_FALSE(validStep(0));
    EXPECT_FALSE(validStep(-1));
    EXPECT_TRUE(validStep(1));
    EXPECT_TRUE(validStep(99));
}

TEST(ReportRules, QuantitiesMustBeNonNegative) {
    EXPECT_TRUE(validQty(0, 0, 0));
    EXPECT_TRUE(validQty(10, 5, 2));
    EXPECT_FALSE(validQty(-1, 0, 0)); // good 为负
    EXPECT_FALSE(validQty(0, -1, 0)); // defect 为负
    EXPECT_FALSE(validQty(0, 0, -1)); // scrap 为负
}

TEST(ReportRules, TotalDeltaSumsAllThree) {
    EXPECT_EQ(totalDelta(10, 5, 2), 17);
    EXPECT_EQ(totalDelta(0, 0, 0), 0);
    EXPECT_EQ(totalDelta(100, 0, 50), 150);
}

TEST(ReportRules, OverReportRejectsAbovePlan) {
    EXPECT_FALSE(overReport(90, 100, 10)); // 恰好满量 -> 不超报
    EXPECT_FALSE(overReport(90, 100, 9));  // 未满 -> 不超报
    EXPECT_TRUE(overReport(95, 100, 10));  // 超报
    EXPECT_TRUE(overReport(100, 100, 1));  // 已满再报 -> 超报
}

TEST(ReportRules, ReachesPlanTriggersCompletion) {
    EXPECT_FALSE(reachesPlan(90, 100, 9));
    EXPECT_TRUE(reachesPlan(90, 100, 10)); // 恰好满量 -> 完工
    EXPECT_TRUE(reachesPlan(90, 100, 11)); // 超出也视为满量 (防超报先拦截)
    EXPECT_TRUE(reachesPlan(100, 100, 0));
}

// ==================== QcRules (质量) ====================

TEST(QcRules, FirstPassRate) {
    EXPECT_DOUBLE_EQ(firstPassRate(100, 95), 95.0);
    EXPECT_DOUBLE_EQ(firstPassRate(10, 3), 30.0);
    EXPECT_DOUBLE_EQ(firstPassRate(0, 0), 0.0); // 空集不除零
    EXPECT_DOUBLE_EQ(firstPassRate(50, 50), 100.0);
}

TEST(QcRules, DispositionRange) {
    EXPECT_TRUE(validDisposition(1)); // 返工
    EXPECT_TRUE(validDisposition(4)); // 让步
    EXPECT_FALSE(validDisposition(0));
    EXPECT_FALSE(validDisposition(5));
    EXPECT_FALSE(validDisposition(-1));
}

// ==================== IntegRules (集成) ====================

TEST(IntegRules, ParseMethodPath) {
    auto [m1, p1] = parseMethodPath("POST /api/v1/erp/orders");
    EXPECT_EQ(m1, "POST");
    EXPECT_EQ(p1, "/api/v1/erp/orders");

    auto [m2, p2] = parseMethodPath("GET /x?q=1");
    EXPECT_EQ(m2, "GET");
    EXPECT_EQ(p2, "/x?q=1");

    // 无空格 (历史数据): 默认 POST, 整串为 path
    auto [m3, p3] = parseMethodPath("/api/v1/wms/stock-in");
    EXPECT_EQ(m3, "POST");
    EXPECT_EQ(p3, "/api/v1/wms/stock-in");
}

TEST(IntegRules, SystemTypeFilterSentinel) {
    EXPECT_TRUE(validSystemType("ERP"));
    EXPECT_TRUE(validSystemType("WMS"));
    EXPECT_FALSE(validSystemType(""));
    EXPECT_FALSE(validSystemType("erp")); // 大小写敏感, 非白名单视为不过滤
    EXPECT_FALSE(validSystemType("MES"));
}

TEST(IntegRules, StatusFilterSentinel) {
    EXPECT_TRUE(validStatusFilter(0));
    EXPECT_TRUE(validStatusFilter(1));
    EXPECT_TRUE(validStatusFilter(2));
    EXPECT_FALSE(validStatusFilter(-1));
    EXPECT_FALSE(validStatusFilter(3));
    EXPECT_FALSE(validStatusFilter(99));
}
