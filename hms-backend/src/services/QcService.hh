#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>

// 质量域服务 (计划任务 21 / 设计文档 4.8 节 7 接口):
// 检验标准查询、检验录入(事务内含缺陷)、检验列表/详情、缺陷列表/处置、质量统计。
// 回调式接口, 控制器层仅做参数解析与响应封装。
namespace hms::QcService {

using JsonCb = std::function<void(const nlohmann::json&)>;
using ErrCb = std::function<void(int, const std::string&)>;

// ---- 检验标准 ----
void listStandards(int page, int pageSize, const std::string& keyword, int64_t productId,
                   JsonCb onOk, ErrCb onErr);

// ---- 检验记录 ----
void createInspection(const nlohmann::json& body, int64_t inspectorId, JsonCb onOk, ErrCb onErr);
void listInspections(int page, int pageSize, int64_t workOrderId, int result, JsonCb onOk,
                     ErrCb onErr);
void getInspection(int64_t id, JsonCb onOk, ErrCb onErr);

// ---- 缺陷 ----
void listDefects(int page, int pageSize, int64_t workOrderId, int disposition,
                 const std::string& category, JsonCb onOk, ErrCb onErr);
void handleDefect(int64_t id, const nlohmann::json& body, int64_t userId, JsonCb onOk,
                  ErrCb onErr);

// ---- 统计 ----
void statistics(const std::string& startDate, const std::string& endDate, JsonCb onOk,
                ErrCb onErr);

} // namespace hms::QcService
