#pragma once

#include <drogon/utils/coroutine.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

// 工单服务 (计划任务 14/15 / 设计文档 4.6, 7.5 节)。
// 约定: Service 层用 Drogon 协程写事务逻辑; 业务错误抛 ApiException,
// 由 main.cc 全局错误拦截器统一转响应信封。
namespace hms::WorkOrderService {

// JWT 注入的用户上下文 (JwtMiddleware 写入请求属性)
struct UserCtx {
    int64_t userId = 0;
    std::string username;
    int64_t deptId = 0;
    int dataScope = 1;
    std::vector<int64_t> customDeptIds;
};

// 列表 (分页 + data_scope 过滤)
drogon::Task<nlohmann::json> list(int page, int pageSize, int status, int64_t lineId,
                                  const UserCtx& ctx);
// 详情 (含工序行)
drogon::Task<nlohmann::json> detail(int64_t id, const UserCtx& ctx);
// 创建工单 (status=0 待排产, 自动按工艺步骤生成工序行)
drogon::Task<nlohmann::json> create(const nlohmann::json& body, const UserCtx& ctx);
// 修改工单 (仅未下达状态允许)
drogon::Task<nlohmann::json> update(int64_t id, const nlohmann::json& body, const UserCtx& ctx);
// 状态流转: schedule/release/start/pause/complete/close/cancel (表驱动状态机)
drogon::Task<nlohmann::json> transit(int64_t id, const std::string& action, const UserCtx& ctx);
// 报工 (7.5 节事务范式): 工序行级更新 -> 工单汇总 -> 满量自动完工并
// 事务内写 mq_outbox (停采指令), COMMIT 后由 OutboxDispatcher 投递
drogon::Task<nlohmann::json> report(int64_t id, const nlohmann::json& body, const UserCtx& ctx);

} // namespace hms::WorkOrderService
