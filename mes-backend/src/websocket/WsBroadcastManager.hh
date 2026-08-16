#pragma once

#include <drogon/WebSocketController.h>
#include <nlohmann/json.hpp>

#include <string>

// WebSocket 广播管理 (计划任务 21 / 设计文档 4.9 / contracts/ws-push.schema.json):
// - 按频道维护订阅连接; 推送信封 {version:"1.0", channel, ts, payload};
// - 频道级合并推送: 每 tick(200ms) 只发该频道最新一条, 天然 5Hz 上限;
// - 慢连接降级: 单连接在途推送超阈值直接断开 (不拖垮其他订阅者);
// - Redis Pub/Sub 跨实例: 订阅 ws:broadcast:{channel}, 发布方 (AlertHandler 等)
//   PUBLISH 同名频道即可被所有实例转发到本地 WS 连接。
namespace mes::WsBroadcastManager {

// 启动 Redis 订阅线程与合并推送定时器 (registerBeginningAdvice 调用)
void start();
void stop();

// 进程内直接发布 (自动包装信封并经合并窗口推送)
void publish(const std::string& channel, const nlohmann::json& payload);

// 订阅/退订 (WsController 调用)
void subscribe(const std::string& channel, const drogon::WebSocketConnectionPtr& conn);
void unsubscribeAll(const drogon::WebSocketConnectionPtr& conn);
size_t subscriberCount(const std::string& channel);

} // namespace mes::WsBroadcastManager
