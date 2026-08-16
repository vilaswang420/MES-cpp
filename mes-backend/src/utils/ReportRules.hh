#pragma once

// 报工业务规则 (P2-3.2, 纯逻辑 header-only 可单测):
// WorkOrderService::report 的数量校验/超报/满量判定。
// 校验语义与 report 单 SQL 原子更新一致:
//   UPDATE ... SET completed_qty = completed_qty + $1
//   WHERE completed_qty + $1 <= plan_qty  (原子防超报)
namespace mes::ReportRules {

// 工序序号合法性: 必须为正
inline bool validStep(int stepSeq) {
    return stepSeq > 0;
}

// 报工数量合法性: 三类数量均不可为负 (防负数污染累计值)
inline bool validQty(int goodQty, int defectQty, int scrapQty) {
    return goodQty >= 0 && defectQty >= 0 && scrapQty >= 0;
}

// 本次报工总量 (good + defect + scrap), 用于防超报与满量判定
inline int totalDelta(int goodQty, int defectQty, int scrapQty) {
    return goodQty + defectQty + scrapQty;
}

// 超报判定: 累计 + 本次 > 计划 (与 SQL 中 completed_qty + delta <= plan_qty 取反一致)
inline bool overReport(int completedQty, int planQty, int delta) {
    return completedQty + delta > planQty;
}

// 满量判定: 累计 + 本次 >= 计划 (触发工序/工单置完工)
inline bool reachesPlan(int completedQty, int planQty, int delta) {
    return completedQty + delta >= planQty;
}

} // namespace mes::ReportRules
