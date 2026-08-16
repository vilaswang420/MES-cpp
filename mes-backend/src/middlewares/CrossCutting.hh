#pragma once

#include <string>

// 横切关注点装配 (设计文档 5.3)。
// 注意: Drogon 1.9.x 的 HttpMiddleware 只能经路由名约束 (ADD_METHOD_TO 的
// "MiddlewareName" 参数) 逐路由挂载, 没有全局注册机制 (registerMiddleware 仅把
// 实例放入单例表供按名解析), 故 Trace/Jwt/Rbac/Audit 统一改用 AOP advice 实现,
// 语义与原中间件链等价:
//   preHandling 观察者 : Trace (注入 trace_id 与请求开始时间)
//   preHandling 链式   : Jwt -> Rbac (注册顺序即执行顺序, 可拦截请求)
//   postHandling 观察者: Audit (仅成功响应入库, 与洋葱模型中 Audit 位于
//                        Jwt/Rbac 内层、拦截响应不经过它的语义一致)
//   preSending 观察者  : 回写 X-Trace-Id 响应头 (信封响应的兜底)
namespace mes {

// 生成 24 位十六进制 trace_id
std::string genTraceId();

// 安装全部 advice (main.cc 调用一次, 须在 app().run() 之前)
void installCrossCutting();

// 启动审计批量刷盘定时器 (main.cc 调用一次)
void startAuditFlusher();

} // namespace mes
