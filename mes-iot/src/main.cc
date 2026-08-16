// mes-iot 采集服务 (P4-5.1: Modbus TCP 真实采集)
// 架构: ConfigLoader → DevicePoller 线程池 → CmdConsumer → BatchPublisher → healthz
// Linux 目标平台; Windows 仅编译骨架 (healthz + publisher, 无采集)。
#include <nlohmann/json.hpp>
#include <SimpleAmqpClient/SimpleAmqpClient.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "collector/ConfigLoader.hh"
#include "collector/DevicePoller.hh"
#include "cmd/CmdConsumer.hh"
#endif

namespace
{
    std::atomic<bool> g_running{true};

    void signalHandler(int) { g_running = false; }

    // ---- /healthz 极简探针 ----
    void healthzServer(int port)
    {
#ifdef _WIN32
        std::cout << "[mes-iot] healthz skipped on windows build (port " << port << ")\n";
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
        std::cout << "[mes-iot] healthz listening on :" << port << "\n";
        while (g_running)
        {
            int cli = ::accept(srv, nullptr, nullptr);
            if (cli < 0)
                continue;
            char buf[512];
            ::recv(cli, buf, sizeof(buf), 0);
            const char *body = "{\"code\":200,\"message\":\"success\","
                               "\"data\":{\"status\":\"ok\",\"service\":\"mes-iot\"}}";
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

    // ---- 批量发布器 (100 条或 100ms 窗口) ----
    class BatchPublisher
    {
    public:
        BatchPublisher(std::string url, std::string exchange, std::string routingKey, int batchSize,
                       int flushMs)
            : url_(std::move(url)), exchange_(std::move(exchange)),
              routingKey_(std::move(routingKey)), batchSize_(batchSize), flushMs_(flushMs) {}

        void start()
        {
            worker_ = std::thread([this] { loop(); });
        }

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
            AmqpClient::Channel::ptr_t channel;
            while (g_running)
            {
                std::vector<std::string> batch;
                {
                    std::unique_lock lock(mu_);
                    cv_.wait_for(lock, std::chrono::milliseconds(flushMs_),
                                 [this] { return !queue_.empty() || !g_running; });
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
                        channel = AmqpClient::Channel::CreateFromUri(url_);
                        channel->DeclareExchange(exchange_,
                                                 AmqpClient::Channel::EXCHANGE_TYPE_TOPIC,
                                                 true /*passive: 由 topology.json 预声明*/);
                    }
                    for (const auto &payload : batch)
                    {
                        auto msg = AmqpClient::BasicMessage::Create(payload);
                        msg->ContentType("application/json");
                        msg->DeliveryMode(AmqpClient::BasicMessage::delivery_mode_t::dm_persistent);
                        channel->BasicPublish(exchange_, routingKey_, msg);
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[mes-iot] publish failed: " << e.what() << "\n";
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
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

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

#ifndef _WIN32
    // ---- P4-5.1: Modbus TCP 真实采集管线 ----
    std::string backendUrl = cfg.value("backend_url", "http://127.0.0.1:8088");
    std::string backendUser = cfg.value("backend_user", "admin");
    std::string backendPwd = cfg.value("backend_pwd", "");

    if (backendPwd.empty())
    {
        std::cerr << "[mes-iot] backend_pwd not set in config; cannot load device config\n";
        std::cerr << "[mes-iot] running in skeleton mode (publisher + healthz only)\n";
    }
    else
    {
        // Step 1: 从后端 REST 拉取设备+传感器配置
        auto configResult = mes::iot::loadConfig(backendUrl, backendUser, backendPwd);
        if (!configResult.ok)
        {
            std::cerr << "[mes-iot] config load failed: " << configResult.error << "\n";
            std::cerr << "[mes-iot] running in skeleton mode\n";
        }
        else
        {
            // Step 2: 创建 DevicePoller 线程池
            std::vector<std::unique_ptr<mes::iot::DevicePoller>> pollers;
            auto publishFn = [&publisher](const std::string &msg) {
                publisher.enqueue(msg);
            };

            for (auto &devCfg : configResult.devices)
            {
                auto poller = std::make_unique<mes::iot::DevicePoller>(
                    std::move(devCfg), publishFn);
                poller->start();
                pollers.push_back(std::move(poller));
            }

            std::cout << "[mes-iot] started " << pollers.size() << " device poller(s)\n";

            // Step 3: 创建 CmdConsumer (停采/恢复指令)
            mes::iot::CmdConsumer cmdConsumer(
                cfg.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/"),
                cfg.value("exchange", "iot.exchange"));
            for (auto &p : pollers)
            {
                cmdConsumer.registerPoller(p.get());
            }
            cmdConsumer.start();

            std::cout << "[mes-iot] Modbus TCP collection started\n";

            // 等待退出信号
            while (g_running)
                std::this_thread::sleep_for(std::chrono::seconds(1));

            // 优雅关闭
            cmdConsumer.stop();
            for (auto &p : pollers)
                p->stop();

            publisher.stop();
            healthz.join();
            return 0;
        }
    }
#endif

    // 骨架模式 (Windows / 配置加载失败)
    std::cout << "[mes-iot] skeleton started; collectors not available\n";
    while (g_running)
        std::this_thread::sleep_for(std::chrono::seconds(1));

    publisher.stop();
    healthz.join();
    return 0;
}
