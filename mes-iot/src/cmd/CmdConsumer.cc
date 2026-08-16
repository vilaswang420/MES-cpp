// CmdConsumer.cc — 命令消费者实现 (P4-5.1)
// 消费 iot.cmd.collector.queue, 解析 cmd.stop.# / cmd.dev.# 消息,
// 暂停/恢复对应 DevicePoller。
#include "cmd/CmdConsumer.hh"

#include <nlohmann/json.hpp>
#include <SimpleAmqpClient/SimpleAmqpClient.h>

#include <chrono>
#include <iostream>

namespace mes::iot {

namespace {

// 队列名 (与 deploy/mq/topology.json 一致)
constexpr const char* kQueueName = "iot.cmd.collector.queue";
constexpr const char* kBindStop = "cmd.stop.#";
constexpr const char* kBindDev = "cmd.dev.#";

} // namespace

CmdConsumer::CmdConsumer(std::string amqpUrl, std::string exchange)
    : amqpUrl_(std::move(amqpUrl)), exchange_(std::move(exchange)) {}

CmdConsumer::~CmdConsumer() {
    stop();
}

void CmdConsumer::registerPoller(DevicePoller* poller) {
    pollers_[poller->deviceId()] = poller;
}

void CmdConsumer::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { consumeLoop(); });
}

void CmdConsumer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void CmdConsumer::consumeLoop() {
    while (running_.load()) {
        AmqpClient::Channel::ptr_t channel;
        try {
            channel = AmqpClient::Channel::CreateFromUri(amqpUrl_);

            // 声明队列 (passive=true, 由 topology.json 预声明)
            channel->DeclareQueue(kQueueName, true /*passive*/);
            // 绑定 routing keys
            channel->BindQueue(kQueueName, exchange_, kBindStop);
            channel->BindQueue(kQueueName, exchange_, kBindDev);

            // 消费 (basic_consume)
            std::string consumerTag = channel->BasicConsume(
                kQueueName, "" /*tag*/, true /*noLocal*/, false /*noAck*/,
                true /*exclusive*/);

            std::cout << "[cmd] consumer started on " << kQueueName << "\n";

            while (running_.load()) {
                AmqpClient::Envelope::ptr_t env;
                bool got = false;
                try {
                    // 1s 超时, 允许定期检查 running_ 标志
                    got = channel->BasicConsumeMessage(consumerTag, env, 1000 /*ms*/);
                } catch (const AmqpClient::ConsumerCancelledException&) {
                    std::cerr << "[cmd] consumer cancelled by broker, reconnecting...\n";
                    break;
                }

                if (!got || !env) continue; // 超时

                std::string body = env->Message()->Body();
                std::string routingKey = env->RoutingKey();

                try {
                    auto j = nlohmann::json::parse(body);
                    int64_t deviceId = j.value("device_id", static_cast<int64_t>(0));
                    std::string action = j.value("action", "");

                    std::cout << "[cmd] received: routing_key=" << routingKey
                              << " device_id=" << deviceId << " action=" << action << "\n";

                    // 查找对应的 DevicePoller
                    auto it = pollers_.find(deviceId);
                    if (it == pollers_.end()) {
                        std::cerr << "[cmd] device_id=" << deviceId
                                  << " not found in pollers (may not be configured)\n";
                    } else {
                        if (action == "stop") {
                            it->second->pause(); // 幂等: 已停则跳过
                        } else if (action == "resume") {
                            it->second->resume();
                        } else {
                            std::cerr << "[cmd] unknown action: " << action << "\n";
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[cmd] message parse error: " << e.what() << "\n";
                }

                // ACK 消息 (手动确认)
                channel->BasicAck(env);
            }
        } catch (const std::exception& e) {
            std::cerr << "[cmd] AMQP error: " << e.what()
                      << ", reconnecting in 5s...\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    std::cout << "[cmd] consumer loop exited\n";
}

} // namespace mes::iot
