#pragma once

#include <drogon/utils/coroutine.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

// 生产主数据服务 (产线/产品/工艺/计划, P1-2.7 主数据 CRUD 补齐)。
// 约定: 与 WorkOrderService 一致 — Service 层用 Drogon 协程写事务逻辑;
// 业务错误抛 ApiException (common/ApiResponse.hh), 由 Controller 的
// handleError 统一转响应信封。
// 设计要点:
//  * 列表统一分页 (page/page_size, 上限 200) + keyword 模糊过滤;
//  * keyword 用 NULL 哨兵参数化 (($1::text IS NULL OR ...)), 禁止字符串拼接用户输入;
//  * 删除一律软删 (deleted=TRUE), 被工单/工位/计划等引用的数据抛 409 Conflict;
//  * createPlan 单号走 prod_plan_no_seq 全局序列 (防 time(nullptr) 秒级碰撞);
//  * createProcess 用事务协程, 步骤插入失败自动回滚 (修复旧版回调式半提交风险)。
namespace hms::ProductionService {

// ---- 产线 ----
drogon::Task<nlohmann::json> listLines(int page, int pageSize, const std::string& keyword);
drogon::Task<nlohmann::json> createLine(const nlohmann::json& body);
drogon::Task<nlohmann::json> updateLine(int64_t id, const nlohmann::json& body);
drogon::Task<nlohmann::json> deleteLine(int64_t id);
drogon::Task<nlohmann::json> listStations(int64_t lineId);

// ---- 产品 ----
drogon::Task<nlohmann::json> listProducts(int page, int pageSize, const std::string& keyword);
drogon::Task<nlohmann::json> createProduct(const nlohmann::json& body);
drogon::Task<nlohmann::json> updateProduct(int64_t id, const nlohmann::json& body);
drogon::Task<nlohmann::json> deleteProduct(int64_t id);

// ---- 工艺路线 (含步骤, 事务) ----
drogon::Task<nlohmann::json> listProcesses(int page, int pageSize, const std::string& keyword);
drogon::Task<nlohmann::json> createProcess(const nlohmann::json& body, int64_t createdBy);
drogon::Task<nlohmann::json> updateProcess(int64_t id, const nlohmann::json& body);
drogon::Task<nlohmann::json> deleteProcess(int64_t id);

// ---- 生产计划 ----
drogon::Task<nlohmann::json> listPlans(int page, int pageSize);
drogon::Task<nlohmann::json> createPlan(const nlohmann::json& body, int64_t createdBy);

} // namespace hms::ProductionService
