#include "mq/StopCollectionHandler.hh"

#include <SimpleAmqpClient/Channel.h>
#include <SimpleAmqpClient/Envelope.h>
#include <drogon/drogon.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "common/SqlParam.hh"
#include "mq/OutboxDispatcher.hh"
#include "utils/TimeUtils.hh"

namespace hms::StopCollectionHandler {

namespace {

std::atomic<bool> g_stop{false};
std::thread g_thread;

// ack 队列: drogon DB 回调在其 IO 线程触发, 而 SimpleAmqpClient channel 非线程安全,
// 故回调仅登记结果, 由消费者线程统一 ack/reject (同 DataIngestHandler 模式)
std::mutex g_ackMtx;
std::deque<std::pair<AmqpClient::Envelope::DeliveryInfo, bool>> g_ackQueue;

void enqueueAck(const AmqpClient::Envelope::DeliveryInfo& info, bool ok) {
    std::lock_guard lk(g_ackMtx);
    g_ackQueue.emplace_back(info, ok);
}

// P1-2.2 停采二次投递: 解析 stop_collection 消息 -> 查工单绑定设备
// (工单 line_id -> iot_devices) -> 逐台写 mq_outbox
// (routing_key=cmd.stop.{device_id}, payload 含幂等键 work_order_id+device_id),
// 由 OutboxDispatcher 投递到 iot.exchange, hms-iot 经 cmd.stop.#
// (iot.cmd.collector.queue, 见 topology.json) 独占消费。
// 拓扑隔离: iot.cmd.queue 仅绑定 cmd.stop_collection (原始指令精确 key),
// cmd.stop.# / cmd.dev.# 只进 collector 队列, 后端自投消息不会回环。
// 可靠性: 写 outbox 全部成功才 ack; 任一失败 requeue 重试 (消息不丢)。
void handleStopCollection(const AmqpClient::Envelope::ptr_t& envelope) {
    auto delivery = envelope->GetDeliveryInfo();
    // 防御: 若因拓扑误配收到二次投递消息 (cmd.stop.{id}), 直接 ack 丢弃避免死循环
    if (envelope->RoutingKey().rfind("cmd.stop.", 0) == 0) {
        LOG_WARN << "[stop-collection] unexpected relayed key, drop: " << envelope->RoutingKey();
        enqueueAck(delivery, true);
        return;
    }
    int64_t workOrderId = 0;
    try {
        auto j = nlohmann::json::parse(envelope->Message()->Body());
        workOrderId = j.value("work_order_id", (int64_t)0);
    } catch (const std::exception&) {
    }
    if (workOrderId <= 0) {
        // 坏消息: ack 丢弃 (避免死循环), 由日志暴露
        LOG_WARN << "[stop-collection] invalid payload, drop: " << envelope->Message()->Body();
        enqueueAck(delivery, true);
        return;
    }

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT d.id FROM iot_devices d "
        "JOIN prod_work_orders wo ON wo.line_id = d.line_id "
        "WHERE wo.id = $1 AND d.deleted = FALSE",
        [delivery, workOrderId](const drogon::orm::Result& r) {
            if (r.empty()) {
                LOG_INFO << "[stop-collection] no device bound, ack wo=" << workOrderId;
                enqueueAck(delivery, true);
                return;
            }
            // 逐台设备写 outbox (持久化, Dispatcher 异步投递); 全部成功才 ack
            struct State {
                std::atomic<int> pending{0};
                std::atomic<bool> failed{false};
            };
            auto st = std::make_shared<State>();
            auto db2 = drogon::app().getDbClient();
            for (const auto& row : r) {
                auto deviceId = row["id"].as<int64_t>();
                nlohmann::json msg = {{"version", "1.0"},
                                      {"type", "stop_collection"},
                                      {"device_id", deviceId},
                                      {"work_order_id", workOrderId}, // 幂等键组成部分
                                      {"timestamp", TimeUtils::nowUtcIso()}};
                st->pending.fetch_add(1);
                db2->execSqlAsync(
                    OutboxService::kEnqueueSql,
                    [st, delivery, workOrderId, deviceId](const drogon::orm::Result&) {
                        LOG_INFO << "[stop-collection] enqueued cmd.stop." << deviceId
                                 << " wo=" << workOrderId;
                        if (st->pending.fetch_sub(1) == 1 && !st->failed.load())
                            enqueueAck(delivery, true);
                    },
                    [st, delivery, workOrderId, deviceId](const drogon::orm::DrogonDbException& e) {
                        LOG_WARN << "[stop-collection] outbox enqueue failed device=" << deviceId
                                 << " wo=" << workOrderId << ": " << e.base().what();
                        st->failed.store(true);
                        if (st->pending.fetch_sub(1) == 1)
                            enqueueAck(delivery, false); // requeue 重试
                    },
                    "iot.exchange", "cmd.stop." + std::to_string(deviceId), msg.dump());
            }
        },
        [delivery, workOrderId](const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "[stop-collection] device query failed wo=" << workOrderId << ": "
                     << e.base().what();
            enqueueAck(delivery, false); // requeue 重试
        },
        SqlArg(workOrderId));
}

} // namespace

void start(const nlohmann::json& config) {
    // vhost "/" 必须 URL 编码为 %2F, 否则 rabbitmq-c 解析为空 vhost 导致 NOT_ALLOWED
    auto url = config.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/%2F");
    std::vector<std::string> queues;
    if (config.contains("consume_queues"))
        for (const auto& q : config["consume_queues"])
            queues.push_back(q.get<std::string>());
    if (queues.empty())
        queues.push_back("iot.cmd.queue");

    g_stop = false;
    g_thread = std::thread([url, queues] {
        AmqpClient::Channel::ptr_t channel;
        std::string consumerTag;
        while (!g_stop) {
            try {
                if (!channel) {
                    channel = AmqpClient::Channel::CreateFromUri(url);
                    // 队列由 deploy/mq/topology.json 预声明, 此处 passive 校验
                    for (const auto& q : queues)
                        channel->DeclareQueue(q, true /*passive*/);
                    // no_ack=false: 手动 ack, 处理失败可 requeue
                    consumerTag = channel->BasicConsume(queues[0], "", true, false);
                    // 停采指令低频, prefetch=1 保证上一条处理完成(ack)才拉下一条
                    channel->BasicQos(consumerTag, 1);
                    LOG_INFO << "stop-collection consumer connected";
                }
                // 先统一处理 ack 队列 (channel 仅消费者线程触碰)
                {
                    std::deque<std::pair<AmqpClient::Envelope::DeliveryInfo, bool>> acks;
                    {
                        std::lock_guard lk(g_ackMtx);
                        acks.swap(g_ackQueue);
                    }
                    for (const auto& [info, ok] : acks) {
                        if (ok)
                            channel->BasicAck(info);
                        else
                            channel->BasicReject(info, true /*requeue*/);
                    }
                }
                // 阻塞拉取, 1s 超时轮询退出标志
                AmqpClient::Envelope::ptr_t envelope;
                if (channel->BasicConsumeMessage(consumerTag, envelope, 1000) && envelope) {
                    LOG_INFO << "[stop-collection] routing_key=" << envelope->RoutingKey()
                             << " payload=" << envelope->Message()->Body();
                    handleStopCollection(envelope);
                }
            } catch (const std::exception& e) {
                LOG_WARN << "stop-collection consumer unavailable: " << e.what();
                channel.reset();
                consumerTag.clear();
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    });
    g_thread.detach();
}

void stop() {
    g_stop = true;
}

} // namespace hms::StopCollectionHandler
