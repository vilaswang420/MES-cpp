// ConfigLoader.hh — 配置加载器 (P4-5.1)
// 启动时经后端 REST 拉取设备+传感器配置 (禁止文件与 DB 双写)。
// iot.json 仅留基础设施配置: amqp_url / backend_url / healthz_port
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hms::iot {

// 传感器配置 (从 DB iot_sensors 表拉取)
struct SensorConfig {
    int64_t sensor_id = 0;
    std::string sensor_code;
    std::string sensor_name;
    std::string data_type;       // "int16" / "uint16" / "int32" / "float32"
    std::string unit;
    int register_addr = 0;       // Modbus 寄存器地址 (如 40001)
    double scale_factor = 1.0;   // 缩放因子
    int sample_interval = 1000;  // 采样间隔 (ms, 预留)
};

// 设备配置 (从 DB iot_devices 表拉取)
struct DeviceConfig {
    int64_t device_id = 0;
    std::string device_code;
    std::string device_name;
    std::string protocol;        // "modbus_tcp"
    std::string ip_address;
    int port = 502;
    int unit_id = 1;             // Modbus slave unit ID (from connection_config JSON)
    int poll_interval_ms = 1000; // 轮询间隔
    std::vector<SensorConfig> sensors;
};

// 配置加载结果
struct ConfigLoadResult {
    bool ok = false;
    std::string error;
    std::vector<DeviceConfig> devices;
};

// 从后端 REST 拉取设备+传感器配置
// backendUrl: 如 "http://127.0.0.1:8088"
// username/password: 服务账号 (需 iot:device:list 权限)
ConfigLoadResult loadConfig(const std::string& backendUrl,
                            const std::string& username,
                            const std::string& password);

} // namespace hms::iot
