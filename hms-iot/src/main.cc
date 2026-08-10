// hms-iot 采集服务骨架 (计划任务 18, M2 实施)。
// 目标并发模型: epoll 事件循环 + 协议 worker 线程池;
// 采集线程就地缩放/校验 -> 发布线程批量 publish (>=100 条或 100ms 窗口,
// publisher confirms); 消息严格遵守 contracts/iot-message.schema.json。
//
// M0 阶段交付: /healthz 探针 + 配置加载 + 批量发布器骨架;
// Modbus TCP 采集实现与停采指令消费在 M2 接入 (先用 scripts/iot_simulator.py 联调)。
#include <nlohmann/json.hpp>
#include <SimpleAmqpClient/SimpleAmqpClient.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{

    std::atomic<bool> g_running{true};

    // ---- /healthz 极简探针 (M0 出口: 四服务 healthz 200) ----
    void healthzServer(int port)
    {
#ifdef _WIN32
        std::cout << "[hms-iot] healthz skipped on windows build (port " << port << ")\n";
        while (g_running)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        return;
#else
        int srv = ::socket(AF_INET, SOCK_STREAM, 0);
        if (srv < 0)
            return;
        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (::bind(srv, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
            ::listen(srv, 16) < 0)
        {
            ::close(srv);
            return;
        }
        std::cout << "[hms-iot] healthz listening on :" << port << "\n";
        while (g_running)
        {
            int cli = ::accept(srv, nullptr, nullptr);
            if (cli < 0)
                continue;
            char buf[512];
            ::recv(cli, buf, sizeof(buf), 0);
            const char *body = "{\"code\":200,\"message\":\"success\","
                               "\"data\":{\"status\":\"ok\",\"service\":\"hms-iot\"}}";
            char resp[1024];
            int len = std::snprintf(resp, sizeof(resp),
                                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                    "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
                                    std::strlen(body), body);
            ::send(cli, resp, len, 0);
            ::close(cli);
        }
        ::close(srv);
#endif
    }

    // ---- 批量发布器骨架 (M2: 接入真实采集管道) ----
    class BatchPublisher
    {
    public:
        BatchPublisher(std::string url, std::string exchange, std::string routingKey, int batchSize,
                       int flushMs)
            : url_(std::move(url)), exchange_(std::move(exchange)),
              routingKey_(std::move(routingKey)), batchSize_(batchSize), flushMs_(flushMs) {}

        void start()
        {
            worker_ = std::thread([this]
                                  { loop(); });
        }

        // 采集线程入口: 就地缩放/校验后入队 (schema 见 contracts/iot-message.schema.json)
        void enqueue(std::string messageJson)
        {
            {
                std::lock_guard lock(mu_);
                queue_.push(std::move(messageJson));
            }
            cv_.notify_one();
        }

        void stop()
        {
            g_running = false;
            cv_.notify_all();
            if (worker_.joinable())
                worker_.join();
        }

    private:
        void loop()
        {
            Amqp::Channel::ptr_t channel;
            while (g_running)
            {
                std::vector<std::string> batch;
                {
                    std::unique_lock lock(mu_);
                    cv_.wait_for(lock, std::chrono::milliseconds(flushMs_),
                                 [this]
                                 { return !queue_.empty() || !g_running; });
                    while (!queue_.empty() && (int)batch.size() < batchSize_)
                    {
                        batch.push_back(std::move(queue_.front()));
                        queue_.pop();
                    }
                }
                if (batch.empty())
                    continue;
                try
                {
                    if (!channel)
                    {
                        channel = Amqp::Channel::CreateFromUri(url_);
                        channel->DeclareExchange(exchange_, Amqp::Channel::EXCHANGE_TYPE_TOPIC,
                                                 true /*passive: 由 topology.json 预声明*/);
                    }
                    for (const auto &payload : batch)
                    {
                        Amqp::Message msg(payload);
                        msg.setContentType("application/json");
                        msg.setDeliveryMode(Amqp::Message::dm_persistent);
                        channel->PublishMessage(exchange_, routingKey_, msg);
                    }
                    // M2: publisher confirms 批量确认 + 失败重入队策略
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[hms-iot] publish failed: " << e.what() << "\n";
                    channel.reset();
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }

        std::string url_, exchange_, routingKey_;
        int batchSize_, flushMs_;
        std::mutex mu_;
        std::condition_variable cv_;
        std::queue<std::string> queue_;
        std::thread worker_;
    };

} // namespace

int main(int argc, char **argv)
{
    std::string cfgPath = argc > 1 ? argv[1] : "config/iot.json";
    nlohmann::json cfg;
    {
        std::ifstream f(cfgPath);
        if (!f.is_open())
        {
            std::cerr << "config not found: " << cfgPath << "\n";
            return 1;
        }
        f >> cfg;
    }

    BatchPublisher publisher(cfg.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/"),
                             cfg.value("exchange", "iot.exchange"),
                             cfg.value("routing_key", "data.report"),
                             cfg.value("batch_size", 100),
                             cfg.value("flush_interval_ms", 100));
    publisher.start();

    std::thread healthz(healthzServer, cfg.value("healthz_port", 8091));

    // TODO(M2 任务 18): epoll 事件循环 + Modbus TCP worker 线程池;
    // 每设备按 poll_interval_ms 轮询, 采集线程就地 scale/校验后 publisher.enqueue();
    // TODO(M2 任务 19): 消费 cmd.# 停采指令并暂停对应设备轮询。
    std::cout << "[hms-iot] skeleton started; collectors pending M2 implementation\n";

    while (g_running)
        std::this_thread::sleep_for(std::chrono::seconds(1));

    publisher.stop();
    healthz.join();
    return 0;
}
