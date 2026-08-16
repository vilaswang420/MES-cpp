#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <fstream>

#include "common/ApiResponse.hh"
#include "metrics/MetricsCollector.hh"
#include "middlewares/CrossCutting.hh"
#include "middlewares/perm_routes.hh"
#include "mq/AlertHandler.hh"
#include "mq/DataIngestHandler.hh"
#include "mq/MqProducer.hh"
#include "mq/OutboxDispatcher.hh"
#include "mq/StopCollectionHandler.hh"
#include "utils/JwtUtils.hh"
#include "websocket/WsBroadcastManager.hh"

// hms-backend 入口 (计划任务 7/10/15):
// 横切设施装配 = AOP advice 链 (Trace -> Jwt -> Rbac -> Audit) + 统一错误拦截
// (Drogon 1.9.x 中间件无全局注册机制, 见 CrossCutting.hh 注释);
// 启动时初始化权限映射表、JWT/MQ 投递器与停采消费占位。
int main(int argc, char** argv) {
    std::string configPath = argc > 1 ? argv[1] : "config/drogon_config.json";
    drogon::app().loadConfigFile(configPath);

    // ---- JWT 初始化 (密钥与 TTL 来自 custom_config; 生产经部署配置覆盖) ----
    const auto& custom = drogon::app().getCustomConfig();
    hms::JwtUtils::init(custom["jwt_secret"].asString(), custom["jwt_access_ttl_sec"].asInt(),
                        custom["jwt_refresh_ttl_sec"].asInt());

    // ---- fail-closed 权限映射表 (唯一事实源, CI 门禁比对目标) ----
    hms::PermRoutes::init();

    // ---- 全局横切: Trace -> Jwt -> Rbac -> Audit (AOP advice 实现) ----
    hms::installCrossCutting();

    // ---- 全局错误拦截: 业务禁止手拼错误 JSON, 一律经此转统一信封 ----
    // handler 抛出的异常 (含 ApiException)
    drogon::app().setExceptionHandler([](const std::exception& e, const drogon::HttpRequestPtr& req,
                                         std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
        auto traceId = hms::traceIdOf(req);
        if (auto apiEx = dynamic_cast<const hms::ApiException*>(&e)) {
            cb(hms::ApiResponse::error(apiEx->code(), apiEx->what(), traceId));
            return;
        }
        LOG_ERROR << "unhandled exception: " << e.what();
        cb(hms::ApiResponse::error(500, "服务内部错误", traceId));
    });
    // 框架级错误 (404/405/400 等) 同样走统一信封
    drogon::app().setCustomErrorHandler(
        [](drogon::HttpStatusCode code, const drogon::HttpRequestPtr& req) {
            std::string msg = "请求错误";
            if (code == drogon::k404NotFound)
                msg = "资源不存在";
            else if (code == drogon::k405MethodNotAllowed)
                msg = "方法不允许";
            return hms::ApiResponse::error(static_cast<int>(code), msg, hms::traceIdOf(req));
        });

    // ---- 启动后装配 MQ 与定时任务 ----
    drogon::app().registerBeginningAdvice([] {
        // rabbitmq.json 独立于 drogon 配置 (jsoncpp 不支持自定义嵌套解析差异)
        nlohmann::json mqCfg = nlohmann::json::object();
        std::ifstream f("config/rabbitmq.json");
        if (f.is_open()) {
            try {
                f >> mqCfg;
            } catch (const std::exception& e) {
                LOG_WARN << "parse rabbitmq.json failed: " << e.what();
            }
        }
        hms::MqProducer::init(mqCfg);
        hms::OutboxDispatcher::start();           // outbox 扫描投递 (advisory lock 互斥)
        hms::DataIngestHandler::start(mqCfg);     // IoT 数据入库 (任务 19)
        hms::AlertHandler::start(mqCfg);          // 告警落库+广播 (任务 19)
        hms::StopCollectionHandler::start(mqCfg); // 停采二次投递: stop_collection -> cmd.stop.{id}
        hms::WsBroadcastManager::start();         // WS 广播: Redis 订阅+合并推送 (任务 21)
        hms::startAuditFlusher();                 // 审计批量刷盘
        hms::MetricsCollector::start();           // Prometheus 指标采集 (任务 28)
        LOG_INFO << "hms-backend started";
    });

    drogon::app().run();

    hms::DataIngestHandler::stop();
    hms::AlertHandler::stop();
    hms::StopCollectionHandler::stop();
    hms::WsBroadcastManager::stop();
    hms::MetricsCollector::stop();
    hms::MqProducer::shutdown();
    return 0;
}
