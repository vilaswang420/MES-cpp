#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>

// IoT 设备域服务 (计划任务 20 / 设计文档 4.7 节 18 接口):
// 设备/传感器 CRUD、实时与历史数据、告警查询与确认、指令下发 (outbox 事务投递)、
// 采集任务管理。回调式接口, 控制器层仅做参数解析与响应封装。
namespace mes::IotService {

using JsonCb = std::function<void(const nlohmann::json&)>;
using ErrCb = std::function<void(int, const std::string&)>;

// ---- 设备 ----
void listDevices(int page, int pageSize, const std::string& keyword, int status, int64_t lineId,
                 JsonCb onOk, ErrCb onErr);
void getDevice(int64_t id, JsonCb onOk, ErrCb onErr);
void createDevice(const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void updateDevice(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void deleteDevice(int64_t id, JsonCb onOk, ErrCb onErr);
void deviceStatus(int64_t id, JsonCb onOk, ErrCb onErr);

// ---- 传感器 ----
void listSensors(int64_t deviceId, JsonCb onOk, ErrCb onErr);
void addSensor(int64_t deviceId, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void updateSensor(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void deleteSensor(int64_t id, JsonCb onOk, ErrCb onErr); // 软删; 被 OEE 引用/近期有数据 -> 409

// ---- 采集数据 ----
void realtimeData(int64_t deviceId, JsonCb onOk, ErrCb onErr);
void sensorHistory(int64_t sensorId, const std::string& startTime, const std::string& endTime,
                   const std::string& interval, const std::string& agg, JsonCb onOk, ErrCb onErr);

// ---- 告警 ----
void listAlerts(int page, int pageSize, int status, int level, int64_t deviceId, JsonCb onOk,
                ErrCb onErr);
void acknowledgeAlert(int64_t id, int64_t userId, JsonCb onOk, ErrCb onErr); // 0 -> 1
void resolveAlert(int64_t id, int64_t userId, JsonCb onOk, ErrCb onErr);     // 1 -> 2 消除
void dismissAlert(int64_t id, int64_t userId, JsonCb onOk, ErrCb onErr);     // 0,1 -> 3 忽略

// ---- 指令下发 ----
void sendCommand(int64_t deviceId, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);

// ---- 采集任务 ----
void listTasks(int page, int pageSize, JsonCb onOk, ErrCb onErr);
void createTask(const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void updateTask(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr);
void deleteTask(int64_t id, JsonCb onOk, ErrCb onErr);
void toggleTask(int64_t id, bool enabled, JsonCb onOk, ErrCb onErr);

} // namespace mes::IotService
