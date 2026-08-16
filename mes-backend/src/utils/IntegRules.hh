#pragma once

// 集成域规则 (P2-3.2, 纯逻辑 header-only 可单测):
// IntegrationService::retryLog 的 "METHOD path" 解析 + listLogs 过滤哨兵判定。
#include <string>
#include <utility>

namespace mes::IntegRules {

// 解析同步日志 request_url (存 "METHOD path"): 拆出 method 与 path。
// 无空格时按历史语义默认 POST, 整串作 path。
inline std::pair<std::string, std::string> parseMethodPath(const std::string& url) {
    auto sp = url.find(' ');
    if (sp == std::string::npos)
        return {"POST", url};
    return {url.substr(0, sp), url.substr(sp + 1)};
}

// 过滤哨兵: systemType 仅允许 ERP/WMS (其他值视为不过滤)
inline bool validSystemType(const std::string& systemType) {
    return systemType == "ERP" || systemType == "WMS";
}

// 过滤哨兵: status 仅允许 0-2 (0失败/1成功/2待重试), 其他值视为不过滤
inline bool validStatusFilter(int status) {
    return status >= 0 && status <= 2;
}

} // namespace mes::IntegRules
