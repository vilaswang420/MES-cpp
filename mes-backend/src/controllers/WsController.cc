#include <drogon/WebSocketController.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "utils/JwtUtils.hh"
#include "websocket/WsBroadcastManager.hh"

// WebSocket 控制器 (计划任务 21 / 功能清单 4.9):
// 提供 /ws?token=JWT 与 /ws/dashboard?token=JWT 两个端点 (鉴权逻辑一致,
// 在 handleNewConnection 中校验 query token, 失败即断开连接);
// 支持订阅消息 {"action":"subscribe","channel":"..."} 与 {"action":"subscribe","channels":[...]}
// 推送统一遵循标准信封 (contracts/ws-push.schema.json)
namespace mes {

namespace {

// 允许订阅的频道 (与 ws-push.schema.json enum 一致); 其余频道一律拒绝
bool channelAllowed(const std::string& ch) {
    return ch == "production.realtime" || ch == "device.status" || ch == "alert" ||
           ch == "workorder.event";
}

void sendText(const drogon::WebSocketConnectionPtr& conn, const std::string& text) {
    conn->send(text, drogon::WebSocketMessageType::Text);
}

} // namespace

class WsDashboardController : public drogon::WebSocketController<WsDashboardController> {
  public:
    void handleNewConnection(const drogon::HttpRequestPtr& req,
                             const drogon::WebSocketConnectionPtr& conn) override {
        // 本端点无 WebSocket 握手 Authorization 头, 改用 query token 鉴权
        auto token = req->getParameter("token");
        auto payload = token.empty() ? std::nullopt : JwtUtils::verifyAccessToken(token);
        if (!payload) {
            sendText(conn,
                     nlohmann::json{{"type", "error"}, {"message", "token 无效或已过期"}}.dump());
            conn->shutdown();
            return;
        }
        conn->setContext(std::make_shared<int64_t>(payload->userId));
        sendText(conn, nlohmann::json{{"type", "welcome"}, {"user", payload->username}}.dump());
        LOG_INFO << "[ws] connection accepted user=" << payload->username;
    }

    void handleNewMessage(const drogon::WebSocketConnectionPtr& conn, std::string&& message,
                          const drogon::WebSocketMessageType& type) override {
        if (type != drogon::WebSocketMessageType::Text)
            return;
        nlohmann::json msg;
        try {
            msg = nlohmann::json::parse(message);
        } catch (...) {
            return; // 消息非 JSON, 忽略
        }
        if (msg.value("action", "") != "subscribe")
            return;
        // 兼容单频道 (channel) 与批量 (channels) 两种写法
        std::vector<std::string> channels;
        if (msg.contains("channel") && msg["channel"].is_string())
            channels.push_back(msg["channel"].get<std::string>());
        if (msg.contains("channels") && msg["channels"].is_array())
            for (const auto& c : msg["channels"])
                if (c.is_string())
                    channels.push_back(c.get<std::string>());

        nlohmann::json okList = nlohmann::json::array();
        for (const auto& ch : channels) {
            if (!channelAllowed(ch)) {
                sendText(
                    conn,
                    nlohmann::json{{"type", "error"}, {"message", "频道不允许: " + ch}}.dump());
                continue;
            }
            WsBroadcastManager::subscribe(ch, conn);
            okList.push_back(ch);
        }
        if (!okList.empty())
            sendText(conn, nlohmann::json{{"type", "subscribed"}, {"channels", okList}}.dump());
    }

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override {
        WsBroadcastManager::unsubscribeAll(conn);
    }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws");
    WS_PATH_ADD("/ws/dashboard");
    WS_PATH_LIST_END
};

} // namespace mes
