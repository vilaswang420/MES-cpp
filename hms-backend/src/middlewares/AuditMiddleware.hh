#pragma once

#include <drogon/HttpMiddleware.h>

// 操作审计中间件 (计划任务 10):
// 记录写操作与敏感读操作, 异步批量写入 sys_audit_logs 分区表;
// 写入失败只记日志不阻断请求。
namespace hms {

class AuditMiddleware : public drogon::HttpMiddleware<AuditMiddleware> {
  public:
    void invoke(const drogon::HttpRequestPtr& req, MiddlewareNextCallback&& nextCb,
                MiddlewareCallback&& mcb) override;
};

// 启动批量刷盘定时器 (main.cc 调用一次)
void startAuditFlusher();

} // namespace hms
