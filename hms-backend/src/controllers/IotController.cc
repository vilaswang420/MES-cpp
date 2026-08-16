#include <drogon/HttpController.h>

#include "controllers/Common.hh"
#include "services/IotService.hh"

namespace hms {

// IoT 设备域 (计划任务 20 / 设计文档 4.7 节 18 接口)
class IotController : public drogon::HttpController<IotController> {
  public:
    METHOD_LIST_BEGIN
    // 设备
    ADD_METHOD_TO(IotController::listDevices, "/api/v1/iot/devices", drogon::Get);
    ADD_METHOD_TO(IotController::getDevice, "/api/v1/iot/devices/{1}", drogon::Get);
    ADD_METHOD_TO(IotController::createDevice, "/api/v1/iot/devices", drogon::Post);
    ADD_METHOD_TO(IotController::updateDevice, "/api/v1/iot/devices/{1}", drogon::Put);
    ADD_METHOD_TO(IotController::deleteDevice, "/api/v1/iot/devices/{1}", drogon::Delete);
    ADD_METHOD_TO(IotController::deviceStatus, "/api/v1/iot/devices/{1}/status", drogon::Get);
    ADD_METHOD_TO(IotController::sendCommand, "/api/v1/iot/devices/{1}/command", drogon::Post);
    // 传感器
    ADD_METHOD_TO(IotController::listSensors, "/api/v1/iot/devices/{1}/sensors", drogon::Get);
    ADD_METHOD_TO(IotController::addSensor, "/api/v1/iot/devices/{1}/sensors", drogon::Post);
    ADD_METHOD_TO(IotController::updateSensor, "/api/v1/iot/sensors/{1}", drogon::Put);
    ADD_METHOD_TO(IotController::deleteSensor, "/api/v1/iot/sensors/{1}", drogon::Delete);
    // 采集数据
    ADD_METHOD_TO(IotController::realtimeData, "/api/v1/iot/devices/{1}/realtime-data",
                  drogon::Get);
    ADD_METHOD_TO(IotController::sensorHistory, "/api/v1/iot/sensors/{1}/history", drogon::Get);
    // 告警
    ADD_METHOD_TO(IotController::listAlerts, "/api/v1/iot/alerts", drogon::Get);
    ADD_METHOD_TO(IotController::ackAlert, "/api/v1/iot/alerts/{1}/acknowledge", drogon::Put);
    ADD_METHOD_TO(IotController::resolveAlert, "/api/v1/iot/alerts/{1}/resolve", drogon::Put);
    ADD_METHOD_TO(IotController::dismissAlert, "/api/v1/iot/alerts/{1}/dismiss", drogon::Put);
    // 采集任务
    ADD_METHOD_TO(IotController::listTasks, "/api/v1/iot/tasks", drogon::Get);
    ADD_METHOD_TO(IotController::createTask, "/api/v1/iot/tasks", drogon::Post);
    ADD_METHOD_TO(IotController::updateTask, "/api/v1/iot/tasks/{1}", drogon::Put);
    ADD_METHOD_TO(IotController::deleteTask, "/api/v1/iot/tasks/{1}", drogon::Delete);
    ADD_METHOD_TO(IotController::toggleTask, "/api/v1/iot/tasks/{1}/toggle", drogon::Put);
    METHOD_LIST_END

    void listDevices(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        IotService::listDevices(paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                                paramStr(req, "keyword"), paramInt(req, "status", -1),
                                paramInt64(req, "line_id", 0), okCb(callback, traceId),
                                errCb(callback, traceId));
    }

    void getDevice(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::getDevice(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void createDevice(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        IotService::createDevice(body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void updateDevice(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        IotService::updateDevice(id, body, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void deleteDevice(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::deleteDevice(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void deviceStatus(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::deviceStatus(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void sendCommand(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        IotService::sendCommand(id, body, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void listSensors(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::listSensors(id, okCb(callback, traceId), errCb(callback, traceId));
    }

    void addSensor(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        IotService::addSensor(id, body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void realtimeData(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::realtimeData(id, okCb(callback, traceId), errCb(callback, traceId));
    }

    void sensorHistory(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::sensorHistory(
            id, paramStr(req, "start_time"), paramStr(req, "end_time"),
            paramStr(req, "interval").empty() ? "5m" : paramStr(req, "interval"),
            paramStr(req, "agg"), notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void listAlerts(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        IotService::listAlerts(paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                               paramInt(req, "status", -1), paramInt(req, "level", 0),
                               paramInt64(req, "device_id", 0), okCb(callback, traceId),
                               errCb(callback, traceId));
    }

    void ackAlert(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::acknowledgeAlert(id, userCtxOf(req).userId, notNullCb(callback, traceId),
                                     errCb(callback, traceId));
    }

    void resolveAlert(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::resolveAlert(id, userCtxOf(req).userId, notNullCb(callback, traceId),
                                 errCb(callback, traceId));
    }

    void dismissAlert(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::dismissAlert(id, userCtxOf(req).userId, notNullCb(callback, traceId),
                                 errCb(callback, traceId));
    }

    void updateSensor(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        IotService::updateSensor(id, body, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void deleteSensor(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::deleteSensor(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void listTasks(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        IotService::listTasks(paramInt(req, "page", 1), paramInt(req, "page_size", 20),
                              okCb(callback, traceId), errCb(callback, traceId));
    }

    void createTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        IotService::createTask(body, okCb(callback, traceId), errCb(callback, traceId));
    }

    void updateTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null())
            return callback(ApiResponse::error(400, "请求体必须是 JSON", traceId));
        IotService::updateTask(id, body, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void deleteTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        IotService::deleteTask(id, notNullCb(callback, traceId), errCb(callback, traceId));
    }

    void toggleTask(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("enabled"))
            return callback(ApiResponse::error(400, "enabled 必填", traceId));
        IotService::toggleTask(id, body["enabled"].get<bool>(), notNullCb(callback, traceId),
                               errCb(callback, traceId));
    }
};

} // namespace hms
