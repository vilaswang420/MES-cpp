#include <drogon/WebSocketController.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "utils/JwtUtils.hh"
#include "websocket/WsBroadcastManager.hh"

// ?? WebSocket ?? (???? 21 / ???? 4.9 ?):
// ?? /ws?token=JWT ? /ws/dashboard?token=JWT (??????????,
// ??? handleNewConnection ? query token ????, ?????);
// ????? {"action":"subscribe","channel":"..."} ? {"action":"subscribe","channels":[...]}
// ?????? (contracts/ws-push.schema.json)?
namespace hms {

namespace {

// ??????? (ws-push.schema.json enum); ??????????
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
        // ??? WebSocket ???? Authorization ?, ? query token ??
        auto token = req->getParameter("token");
        auto payload = token.empty() ? std::nullopt : JwtUtils::verifyAccessToken(token);
        if (!payload) {
            sendText(conn, nlohmann::json{{"type", "error"}, {"message", "token ?????"}}.dump());
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
            return; // ??????
        }
        if (msg.value("action", "") != "subscribe")
            return;
        // ????? (channel) ???? (channels) ????
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
                sendText(conn,
                         nlohmann::json{{"type", "error"}, {"message", "????: " + ch}}.dump());
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

} // namespace hms
