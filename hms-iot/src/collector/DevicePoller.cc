// DevicePoller.cc — 单设备轮询器实现 (P4-5.1)
// 轮询循环: 合帧 → ModbusClient 读取 → scale 缩放 → 组装消息 → enqueue
// 断连: 指数退避重连 (1s → 2s → 4s → ... → 30s 上限)
#include "collector/DevicePoller.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace hms::iot {

namespace {

// Modbus 寄存器地址基准 (40001 → 协议偏移 0)
constexpr int kRegisterBase = 40001;
constexpr int kMaxReconnectBackoffSec = 30;
constexpr int kQualityGood = 192;

} // namespace

DevicePoller::DevicePoller(DeviceConfig config, PublishFn publishFn)
    : config_(std::move(config)), publishFn_(std::move(publishFn)) {
    // 预计算合帧
    cachedFrames_ = mergeFrames();
}

DevicePoller::~DevicePoller() {
    stop();
}

void DevicePoller::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { pollLoop(); });
}

void DevicePoller::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void DevicePoller::pause() {
    if (!paused_.exchange(true)) {
        std::cout << "[poller] device " << config_.device_code << " paused\n";
    }
}

void DevicePoller::resume() {
    if (paused_.exchange(false)) {
        std::cout << "[poller] device " << config_.device_code << " resumed\n";
    }
}

std::vector<ReadFrame> DevicePoller::mergeFrames() {
    std::vector<ReadFrame> frames;

    // 按寄存器地址排序
    std::vector<size_t> sortedIndices;
    for (size_t i = 0; i < config_.sensors.size(); ++i) {
        sortedIndices.push_back(i);
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(),
              [this](size_t a, size_t b) {
                  return config_.sensors[a].register_addr < config_.sensors[b].register_addr;
              });

    ReadFrame current;
    current.startAddr = 0;
    current.quantity = 0;
    bool hasFrame = false;

    for (size_t idx : sortedIndices) {
        int addr = config_.sensors[idx].register_addr;
        uint16_t modbusAddr = static_cast<uint16_t>(addr - kRegisterBase);
        // data_type 对应的寄存器数: int16/uint16 = 1, int32/uint32/float32 = 2
        int regCount = (config_.sensors[idx].data_type == "int32" ||
                        config_.sensors[idx].data_type == "uint32" ||
                        config_.sensors[idx].data_type == "float32")
                           ? 2
                           : 1;

        if (!hasFrame) {
            current.startAddr = modbusAddr;
            current.quantity = static_cast<uint16_t>(regCount);
            current.sensorIndices.push_back(idx);
            hasFrame = true;
        } else {
            // 检查是否连续 (地址差 ≤ 当前帧末尾)
            uint16_t frameEnd = current.startAddr + current.quantity;
            if (modbusAddr == frameEnd && current.quantity + regCount <= 125) {
                // 连续且未超限 → 合入当前帧
                current.quantity += static_cast<uint16_t>(regCount);
                current.sensorIndices.push_back(idx);
            } else {
                // 不连续或超限 → 保存当前帧, 开新帧
                frames.push_back(current);
                current = ReadFrame{};
                current.startAddr = modbusAddr;
                current.quantity = static_cast<uint16_t>(regCount);
                current.sensorIndices = {idx};
            }
        }
    }
    if (hasFrame) frames.push_back(current);

    std::cout << "[poller] device " << config_.device_code << " merged "
              << config_.sensors.size() << " sensors into " << frames.size() << " frame(s)\n";
    return frames;
}

double DevicePoller::extractValue(const SensorConfig& sensor,
                                  const std::vector<uint16_t>& regs,
                                  size_t offsetInFrame) {
    uint16_t raw0 = regs[offsetInFrame];
    double value = 0.0;

    if (sensor.data_type == "uint16") {
        value = static_cast<double>(raw0);
    } else if (sensor.data_type == "int16") {
        value = static_cast<double>(static_cast<int16_t>(raw0));
    } else if (sensor.data_type == "uint32") {
        // 大端序: 高字在前
        uint32_t raw32 = (static_cast<uint32_t>(raw0) << 16) | regs[offsetInFrame + 1];
        value = static_cast<double>(raw32);
    } else if (sensor.data_type == "int32") {
        uint32_t raw32 = (static_cast<uint32_t>(raw0) << 16) | regs[offsetInFrame + 1];
        value = static_cast<double>(static_cast<int32_t>(raw32));
    } else if (sensor.data_type == "float32") {
        uint32_t raw32 = (static_cast<uint32_t>(raw0) << 16) | regs[offsetInFrame + 1];
        float fval;
        std::memcpy(&fval, &raw32, sizeof(fval));
        value = static_cast<double>(fval);
    } else {
        // 默认 uint16
        value = static_cast<double>(raw0);
    }

    // 应用缩放因子
    return value * sensor.scale_factor;
}

std::string DevicePoller::nowUtcIso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

void DevicePoller::pollLoop() {
    ModbusClient client(config_.ip_address, config_.port, config_.unit_id);
    int backoffSec = 1;

    while (running_.load()) {
        // 暂停状态: 不采集, 短睡眠等待恢复
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // 确保连接
        if (!client.isConnected()) {
            if (!client.connect()) {
                std::cerr << "[poller] " << config_.device_code
                          << " connect failed, backoff " << backoffSec << "s\n";
                std::this_thread::sleep_for(std::chrono::seconds(backoffSec));
                backoffSec = std::min(backoffSec * 2, kMaxReconnectBackoffSec);
                continue;
            }
            backoffSec = 1; // 重置退避
        }

        // 逐帧读取
        bool allOk = true;
        std::string ts = nowUtcIso();

        for (const auto& frame : cachedFrames_) {
            ReadResult result = client.readHoldingRegisters(frame.startAddr, frame.quantity);

            if (result.error != ModbusError::Ok) {
                std::cerr << "[poller] " << config_.device_code
                          << " read failed: addr=" << frame.startAddr
                          << " qty=" << frame.quantity
                          << " err=" << static_cast<int>(result.error) << "\n";
                allOk = false;
                break; // 断连, 进入重连
            }

            // 逐传感器拆值
            size_t offsetInFrame = 0;
            for (size_t idx : frame.sensorIndices) {
                const auto& sensor = config_.sensors[idx];
                double value = extractValue(sensor, result.registers, offsetInFrame);

                // 推进偏移
                int regCount = (sensor.data_type == "int32" ||
                                sensor.data_type == "uint32" ||
                                sensor.data_type == "float32")
                                   ? 2
                                   : 1;
                offsetInFrame += static_cast<size_t>(regCount);

                // 组装 iot-message.schema.json 消息
                nlohmann::json msg = {
                    {"version", "1.0"},
                    {"device_id", config_.device_id},
                    {"device_code", config_.device_code},
                    {"sensor_id", sensor.sensor_id},
                    {"value", value},
                    {"quality", kQualityGood},
                    {"ts", ts},
                };
                publishFn_(msg.dump());
            }
        }

        if (!allOk) {
            client.disconnect();
            std::this_thread::sleep_for(std::chrono::seconds(backoffSec));
            backoffSec = std::min(backoffSec * 2, kMaxReconnectBackoffSec);
            continue;
        }

        // 等待下一轮 (poll_interval_ms)
        auto interval = std::chrono::milliseconds(config_.poll_interval_ms);
        auto elapsed = std::chrono::steady_clock::now();
        // 简化: 直接 sleep interval (不考虑读取耗时; 实际可扣减)
        std::this_thread::sleep_for(interval);
    }

    client.disconnect();
    std::cout << "[poller] device " << config_.device_code << " poll loop exited\n";
}

} // namespace hms::iot
