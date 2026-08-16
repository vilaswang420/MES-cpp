#pragma once

// 质量域规则 (P2-3.2, 纯逻辑 header-only 可单测):
// QcService::statistics 直通率计算 + handleDefect 处置值合法性。
#include <cstdint>

namespace mes::QcRules {

// 直通率 (一次合格率): 合格数 / 总数 * 100; 总数为 0 时返回 0 (避免除零)
inline double firstPassRate(int64_t total, int64_t passCnt) {
    return total > 0 ? passCnt * 100.0 / total : 0.0;
}

// 缺陷处置值合法性: 1返工/2返修/3报废/4让步
inline bool validDisposition(int disposition) {
    return disposition >= 1 && disposition <= 4;
}

} // namespace mes::QcRules
