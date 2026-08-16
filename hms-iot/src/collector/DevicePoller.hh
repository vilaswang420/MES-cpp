// DevicePoller.hh — 单设备轮询器 (P4-5.1)
// 每设备一个 DevicePoller 线程: 按 poll_interval_ms 调度, 传感器地址排序合并帧,
// ModbusClient 读取 → scale 缩放 → quality 标记 → publisher.enqueue()
#pragma once

#include "collector/ConfigLoader.hh"
#include "modbus/ModbusClient.hh"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace hms::iot {

// 消息发布回调 (DevicePoller 组装 JSON 后调用)
// 参数: JSON 字符串 (符合 contracts/iot-message.schema.json)
using PublishFn = std::function<void(const std::string&)>;

// 合并后的读取帧 (一组连续地址的传感器)
struct ReadFrame {
    uint16_t startAddr;   // Modbus 协议偏移 (register_addr - 40001)
    uint16_t quantity;    // 寄存器数量 (≤125)
    std::vector<size_t> sensorIndices; // 对应 sensors_ 中的索引
};

class DevicePoller {
public:
    DevicePoller(DeviceConfig config, PublishFn publishFn);
    ~DevicePoller();

    // 启动轮询线程
    void start();

    // 停止轮询线程
    void stop();

    // 暂停采集 (CmdConsumer stop 指令)
    void pause();

    // 恢复采集 (CmdConsumer resume 指令)
    void resume();

    // 是否已暂停
    bool isPaused() const { return paused_.load(); }

    int64_t deviceId() const { return config_.device_id; }
    const std::string& deviceCode() const { return config_.device_code; }

private:
    // 轮询主循环
    void pollLoop();

    // 合并传感器地址为最少帧数
    std::vector<ReadFrame> mergeFrames();

    // 从原始寄存器值提取传感器数值
    double extractValue(const SensorConfig& sensor, const std::vector<uint16_t>& regs,
                        size_t offsetInFrame);

    // 生成 UTC ISO 8601 时间戳
    static std::string nowUtcIso();

    DeviceConfig config_;
    PublishFn publishFn_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::thread thread_;
    std::vector<ReadFrame> cachedFrames_;
};

} // namespace hms::iot
