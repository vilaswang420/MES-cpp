// CmdConsumer.hh — 命令消费者 (P4-5.1)
// 消费 iot.cmd.collector.queue (binding: cmd.stop.# + cmd.dev.#)
// cmd.stop.{device_id}: 暂停对应 DevicePoller
// cmd.dev.{device_id}:  转发设备指令 (预留, M2 可扩展)
// 幂等: 重复停采指令不报错 (DevicePoller 已停则跳过)
#pragma once

#include "collector/DevicePoller.hh"

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>

namespace mes::iot {

class CmdConsumer {
public:
    CmdConsumer(std::string amqpUrl, std::string exchange);
    ~CmdConsumer();

    // 注册 DevicePoller (用于 pause/resume 查找)
    void registerPoller(DevicePoller* poller);

    // 启动消费线程
    void start();

    // 停止消费
    void stop();

private:
    void consumeLoop();

    std::string amqpUrl_;
    std::string exchange_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    // device_id → DevicePoller* 映射
    std::unordered_map<int64_t, DevicePoller*> pollers_;
};

} // namespace mes::iot
