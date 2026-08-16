#pragma once

#include <drogon/utils/coroutine.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

#include "services/WorkOrderService.hh"

// ERP/WMS 集成服务 (计划任务 23 / 设计文档 4.10 / 7.5 / 7.6 节)。
// 外呼一律经熔断器 + 同步日志 (integ_sync_logs); 完工回报走 Saga 编排
// (工单完工 -> ERP 回报 -> WMS 入库, 失败逆序补偿)。
// 对端契约以 WireMock 风格本地桩先行 (scripts/erp_wms_stub.py)。
namespace mes::IntegrationService {

// ERP 订单同步 (增量时间窗): 拉取 -> upsert integ_erp_orders -> 记日志
drogon::Task<nlohmann::json> syncErpOrders(const nlohmann::json& body);

// ERP 订单转工单 (复用 WorkOrderService::create 生成工单与工序行)
drogon::Task<nlohmann::json> convertErpOrder(int64_t orderId, const WorkOrderService::UserCtx& ctx);

// 完工回报 Saga: T1 工单置完工 -> T2 ERP 回报 -> T3 WMS 入库 -> T4 日志;
// 任一步失败逆序补偿并记失败日志
drogon::Task<nlohmann::json> reportCompletionSaga(int64_t workOrderId);

// WMS 领料请求 / 成品入库 (独立调用, 成功后落 integ_wms_inventory 流水)
drogon::Task<nlohmann::json> wmsPickRequest(const nlohmann::json& body);
drogon::Task<nlohmann::json> wmsStockIn(const nlohmann::json& body);

// 同步日志分页查询 / 失败日志重发
drogon::Task<nlohmann::json> listLogs(int page, int pageSize, const std::string& systemType,
                                      int status);
drogon::Task<nlohmann::json> retryLog(int64_t logId);

// 熔断器状态 (运维观测: {"ERP":"CLOSED","WMS":"OPEN"})
nlohmann::json breakerStates();

} // namespace mes::IntegrationService
