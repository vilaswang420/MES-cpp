#include "services/IotService.hh"

#include <drogon/drogon.h>

#include <map>

#include "common/SqlParam.hh"
#include "mq/OutboxDispatcher.hh"
#include "utils/TimeUtils.hh"

namespace hms::IotService {

namespace {

std::string optStr(const drogon::orm::Row& row, const char* field) {
    return row[field].isNull() ? std::string() : row[field].as<std::string>();
}

nlohmann::json parseJsonField(const std::string& s) {
    try {
        return nlohmann::json::parse(s);
    } catch (...) {
        return nlohmann::json();
    }
}

// PG 唯一约束冲突 (23505) -> 409, 其余 500
// 注: 异步回调中 base() 动态转型到 SqlError 不可靠, what() 也不含 SQLSTATE,
// 直接匹配 libpq 错误文案
int errCodeOf(const drogon::orm::DrogonDbException& e) {
    std::string msg = e.base().what();
    if (msg.find("duplicate key value violates unique constraint") != std::string::npos)
        return 409;
    return 500;
}

void clampPage(int& page, int& pageSize) {
    if (page < 1)
        page = 1;
    if (pageSize < 1 || pageSize > 200)
        pageSize = 20;
}

nlohmann::json deviceRow(const drogon::orm::Row& row) {
    return {{"id", row["id"].as<int64_t>()},
            {"device_code", row["device_code"].as<std::string>()},
            {"device_name", row["device_name"].as<std::string>()},
            {"type_id", row["type_id"].isNull() ? 0 : row["type_id"].as<int64_t>()},
            {"type_name", optStr(row, "type_name")},
            {"line_id", row["line_id"].isNull() ? 0 : row["line_id"].as<int64_t>()},
            {"protocol", row["protocol"].as<std::string>()},
            {"ip_address", optStr(row, "ip_address")},
            {"port", row["port"].isNull() ? 0 : row["port"].as<int>()},
            {"status", row["status"].as<int>()},
            {"last_heartbeat_at", optStr(row, "last_heartbeat_at")},
            {"created_at", optStr(row, "created_at")}};
}

nlohmann::json sensorRow(const drogon::orm::Row& row) {
    return {
        {"id", row["id"].as<int64_t>()},
        {"device_id", row["device_id"].as<int64_t>()},
        {"sensor_code", row["sensor_code"].as<std::string>()},
        {"sensor_name", row["sensor_name"].as<std::string>()},
        {"data_type", row["data_type"].as<std::string>()},
        {"unit", optStr(row, "unit")},
        {"register_addr", optStr(row, "register_addr")},
        {"min_value", row["min_value"].isNull() ? nlohmann::json(nullptr)
                                                : nlohmann::json(row["min_value"].as<double>())},
        {"max_value", row["max_value"].isNull() ? nlohmann::json(nullptr)
                                                : nlohmann::json(row["max_value"].as<double>())},
        {"alarm_low", row["alarm_low"].isNull() ? nlohmann::json(nullptr)
                                                : nlohmann::json(row["alarm_low"].as<double>())},
        {"alarm_high", row["alarm_high"].isNull() ? nlohmann::json(nullptr)
                                                  : nlohmann::json(row["alarm_high"].as<double>())},
        {"sample_interval", row["sample_interval"].as<int>()},
        {"is_key_metric", row["is_key_metric"].as<bool>()},
        {"status", row["status"].as<int>()}};
}

constexpr const char* kDeviceBaseCols =
    "d.id, d.device_code, d.device_name, d.type_id, COALESCE(t.type_name,'') AS type_name, "
    "d.line_id, d.protocol, d.ip_address, d.port, d.status, d.last_heartbeat_at, d.created_at";

// 时间桶白名单 (防 SQL 注入: interval 只允许枚举值, 换算为秒后作 $ 参数)
bool intervalToSeconds(const std::string& interval, int64_t& sec) {
    static const std::map<std::string, int64_t> kMap = {{"1m", 60},    {"5m", 300},  {"15m", 900},
                                                        {"30m", 1800}, {"1h", 3600}, {"6h", 21600},
                                                        {"1d", 86400}};
    auto it = kMap.find(interval);
    if (it == kMap.end())
        return false;
    sec = it->second;
    return true;
}

} // namespace

// ============ 设备 ============

void listDevices(int page, int pageSize, const std::string& keyword, int status, int64_t lineId,
                 JsonCb onOk, ErrCb onErr) {
    clampPage(page, pageSize);

    std::string where = "WHERE d.deleted = FALSE";
    bool hasKw = !keyword.empty();
    if (hasKw)
        where += " AND (d.device_code ILIKE $1 OR d.device_name ILIKE $1)";
    if (status >= 0)
        where += " AND d.status = " + std::to_string(status);
    if (lineId > 0)
        where += " AND d.line_id = " + std::to_string(lineId);

    std::string listSql = std::string("SELECT ") + kDeviceBaseCols +
                          " FROM iot_devices d LEFT JOIN iot_device_types t ON t.id = d.type_id " +
                          where + " ORDER BY d.id LIMIT " + std::to_string(pageSize) + " OFFSET " +
                          std::to_string((page - 1) * pageSize);
    std::string countSql = "SELECT COUNT(*) AS cnt FROM iot_devices d " + where;

    auto rowHandler = [page, pageSize, countSql, hasKw, keyword, onOk,
                       onErr](const drogon::orm::Result& r) {
        nlohmann::json listArr = nlohmann::json::array();
        for (const auto& row : r)
            listArr.push_back(deviceRow(row));
        auto db2 = drogon::app().getDbClient();
        auto countOk = [listArr, page, pageSize, onOk](const drogon::orm::Result& cr) {
            onOk({{"list", listArr},
                  {"total", cr.empty() ? 0 : cr[0]["cnt"].as<int64_t>()},
                  {"page", page},
                  {"page_size", pageSize}});
        };
        auto countErr = [onErr](const drogon::orm::DrogonDbException& e) {
            onErr(500, e.base().what());
        };
        if (hasKw)
            db2->execSqlAsync(countSql, countOk, countErr, "%" + keyword + "%");
        else
            db2->execSqlAsync(countSql, countOk, countErr);
    };
    auto rowErr = [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); };

    auto db = drogon::app().getDbClient();
    if (hasKw)
        db->execSqlAsync(listSql, rowHandler, rowErr, "%" + keyword + "%");
    else
        db->execSqlAsync(listSql, rowHandler, rowErr);
}

void getDevice(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        std::string("SELECT ") + kDeviceBaseCols +
            ", d.connection_config, "
            "(SELECT COALESCE(json_agg(s.* ORDER BY s.id), '[]'::json) FROM iot_sensors s "
            " WHERE s.device_id = d.id AND s.deleted = FALSE) AS sensors "
            "FROM iot_devices d LEFT JOIN iot_device_types t ON t.id = d.type_id "
            "WHERE d.id = $1 AND d.deleted = FALSE",
        [onOk](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr); // controller 转 404
            auto row = r[0];
            auto data = deviceRow(row);
            data["connection_config"] = parseJsonField(optStr(row, "connection_config"));
            data["sensors"] = parseJsonField(optStr(row, "sensors"));
            onOk(data);
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id));
}

void createDevice(const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto code = body.value("device_code", "");
    auto name = body.value("device_name", "");
    auto protocol = body.value("protocol", "");
    if (code.empty() || name.empty() || protocol.empty())
        return onErr(400, "device_code/device_name/protocol 必填");

    auto db = drogon::app().getDbClient();
    auto connCfg = body.contains("connection_config") ? body["connection_config"].dump() : "{}";
    auto ip = body.value("ip_address", "");
    auto installedAt = body.value("installed_at", "");
    db->execSqlAsync(
        "INSERT INTO iot_devices (device_code, device_name, type_id, line_id, workstation_id, "
        "ip_address, port, protocol, connection_config, status, installed_at) "
        "VALUES ($1,$2,$3,$4,$5,$6::inet,$7,$8,$9::jsonb,$10,$11::date) RETURNING id",
        [onOk](const drogon::orm::Result& r) { onOk({{"id", r[0]["id"].as<int64_t>()}}); },
        [onErr](const drogon::orm::DrogonDbException& e) {
            onErr(errCodeOf(e) == 409 ? 409 : 500,
                  errCodeOf(e) == 409 ? "设备编码已存在" : e.base().what());
        },
        code, name,
        body.value("type_id", (int64_t)0) > 0 ? SqlArg(body.value("type_id", (int64_t)0))
                                              : SqlArgNull(),
        body.value("line_id", (int64_t)0) > 0 ? SqlArg(body.value("line_id", (int64_t)0))
                                              : SqlArgNull(),
        body.value("workstation_id", (int64_t)0) > 0
            ? SqlArg(body.value("workstation_id", (int64_t)0))
            : SqlArgNull(),
        ip.empty() ? SqlArgNull() : SqlArg(ip), SqlArg(body.value("port", 0)), protocol, connCfg,
        SqlArg(body.value("status", 0)), installedAt.empty() ? SqlArgNull() : SqlArg(installedAt));
}

void updateDevice(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_devices SET "
        "device_name = COALESCE(NULLIF($2,''), device_name), "
        "type_id = CASE WHEN $3::bigint > 0 THEN $3::bigint ELSE type_id END, "
        "line_id = CASE WHEN $4::bigint > 0 THEN $4::bigint ELSE line_id END, "
        "ip_address = COALESCE(NULLIF($5,'')::inet, ip_address), "
        "port = CASE WHEN $6 > 0 THEN $6 ELSE port END, "
        "status = CASE WHEN $7 >= 0 THEN $7::smallint ELSE status END, "
        "connection_config = CASE WHEN $8::jsonb <> '{}'::jsonb THEN $8::jsonb "
        "                         ELSE connection_config END, "
        "updated_at = NOW() "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr);
            onOk({{"id", id}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id), body.value("device_name", ""), SqlArg(body.value("type_id", (int64_t)0)),
        SqlArg(body.value("line_id", (int64_t)0)), SqlArg(body.value("ip_address", "")),
        SqlArg(body.value("port", 0)), SqlArg(body.value("status", -1)),
        body.contains("connection_config") ? body["connection_config"].dump() : "{}");
}

void deleteDevice(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_devices SET deleted = TRUE, updated_at = NOW() "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        [id, onOk](const drogon::orm::Result& r) {
            onOk(r.empty() ? nlohmann::json(nullptr) : nlohmann::json{{"id", id}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id));
}

void deviceStatus(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT status, last_heartbeat_at, device_code FROM iot_devices "
        "WHERE id = $1 AND deleted = FALSE",
        [id, onOk, onErr](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr);
            auto row = r[0];
            auto status = row["status"].as<int>();
            // Redis device:latest:{id} 最新采集快照 (DataIngestHandler 维护, TTL 24h)
            auto rdb = drogon::app().getRedisClient();
            auto key = "device:latest:" + std::to_string(id);
            rdb->execCommandAsync(
                [status, row, onOk](const drogon::nosql::RedisResult& res) {
                    auto data = nlohmann::json{
                        {"status", status},
                        {"online", status == 1},
                        {"last_heartbeat_at", row["last_heartbeat_at"].isNull()
                                                  ? std::string()
                                                  : row["last_heartbeat_at"].as<std::string>()},
                        {"device_code", row["device_code"].as<std::string>()}};
                    if (res.type() != drogon::nosql::RedisResultType::kNil)
                        data["latest"] = parseJsonField(res.asString());
                    else
                        data["latest"] = nullptr;
                    onOk(data);
                },
                [status, row, onOk](const drogon::nosql::RedisException&) {
                    // Redis 不可用降级: 仅返回 DB 状态
                    onOk(nlohmann::json{
                        {"status", status},
                        {"online", status == 1},
                        {"last_heartbeat_at", row["last_heartbeat_at"].isNull()
                                                  ? std::string()
                                                  : row["last_heartbeat_at"].as<std::string>()},
                        {"latest", nullptr}});
                },
                "GET %s", key.c_str());
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id));
}

// ============ 传感器 ============

void listSensors(int64_t deviceId, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT id, device_id, sensor_code, sensor_name, data_type, unit, register_addr, "
        "min_value, max_value, alarm_low, alarm_high, sample_interval, is_key_metric, status "
        "FROM iot_sensors WHERE device_id = $1 AND deleted = FALSE ORDER BY id",
        [onOk](const drogon::orm::Result& r) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& row : r)
                arr.push_back(sensorRow(row));
            onOk(arr);
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(deviceId));
}

void addSensor(int64_t deviceId, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto code = body.value("sensor_code", "");
    auto name = body.value("sensor_name", "");
    auto dataType = body.value("data_type", "");
    if (code.empty() || name.empty() || dataType.empty())
        return onErr(400, "sensor_code/sensor_name/data_type 必填");

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "INSERT INTO iot_sensors (device_id, sensor_code, sensor_name, data_type, unit, "
        "register_addr, scale_factor, addr_offset, min_value, max_value, alarm_low, alarm_high, "
        "alarm_low_low, alarm_high_high, sample_interval, is_key_metric) "
        "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16) RETURNING id",
        [onOk](const drogon::orm::Result& r) { onOk({{"id", r[0]["id"].as<int64_t>()}}); },
        [onErr](const drogon::orm::DrogonDbException& e) {
            onErr(errCodeOf(e) == 409 ? 409 : 500,
                  errCodeOf(e) == 409 ? "同设备下传感器编码已存在" : e.base().what());
        },
        SqlArg(deviceId), code, name, dataType, SqlArg(body.value("unit", "")),
        SqlArg(body.value("register_addr", "")), SqlArg(body.value("scale_factor", 1.0)),
        SqlArg(body.value("addr_offset", 0.0)),
        body.contains("min_value") ? SqlArg(body["min_value"].get<double>()) : SqlArgNull(),
        body.contains("max_value") ? SqlArg(body["max_value"].get<double>()) : SqlArgNull(),
        body.contains("alarm_low") ? SqlArg(body["alarm_low"].get<double>()) : SqlArgNull(),
        body.contains("alarm_high") ? SqlArg(body["alarm_high"].get<double>()) : SqlArgNull(),
        body.contains("alarm_low_low") ? SqlArg(body["alarm_low_low"].get<double>()) : SqlArgNull(),
        body.contains("alarm_high_high") ? SqlArg(body["alarm_high_high"].get<double>())
                                         : SqlArgNull(),
        SqlArg(body.value("sample_interval", 1000)), SqlArg(body.value("is_key_metric", false)));
}

// 传感器编辑 (部分更新: 未传/空值字段保留原值, COALESCE 模式与 updateDevice 一致)
void updateSensor(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_sensors SET "
        "sensor_name = COALESCE(NULLIF($2,''), sensor_name), "
        "unit = COALESCE(NULLIF($3,''), unit), "
        "register_addr = COALESCE(NULLIF($4,''), register_addr), "
        "scale_factor = COALESCE(NULLIF($5::numeric, 0), scale_factor), "
        "addr_offset = CASE WHEN $6::numeric IS NULL THEN addr_offset ELSE $6::numeric END, "
        "min_value = COALESCE($7::numeric, min_value), "
        "max_value = COALESCE($8::numeric, max_value), "
        "alarm_low = COALESCE($9::numeric, alarm_low), "
        "alarm_high = COALESCE($10::numeric, alarm_high), "
        "alarm_low_low = COALESCE($11::numeric, alarm_low_low), "
        "alarm_high_high = COALESCE($12::numeric, alarm_high_high), "
        "sample_interval = CASE WHEN $13 > 0 THEN $13 ELSE sample_interval END, "
        "is_key_metric = COALESCE($14::boolean, is_key_metric), "
        "status = CASE WHEN $15 >= 0 THEN $15::smallint ELSE status END "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        [id, onOk](const drogon::orm::Result& r) {
            onOk(r.empty() ? nlohmann::json(nullptr) : nlohmann::json{{"id", id}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id), body.value("sensor_name", ""), body.value("unit", ""),
        body.value("register_addr", ""), SqlArg(body.value("scale_factor", 0.0)),
        body.contains("addr_offset") ? SqlArg(body["addr_offset"].get<double>()) : SqlArgNull(),
        body.contains("min_value") ? SqlArg(body["min_value"].get<double>()) : SqlArgNull(),
        body.contains("max_value") ? SqlArg(body["max_value"].get<double>()) : SqlArgNull(),
        body.contains("alarm_low") ? SqlArg(body["alarm_low"].get<double>()) : SqlArgNull(),
        body.contains("alarm_high") ? SqlArg(body["alarm_high"].get<double>()) : SqlArgNull(),
        body.contains("alarm_low_low") ? SqlArg(body["alarm_low_low"].get<double>()) : SqlArgNull(),
        body.contains("alarm_high_high") ? SqlArg(body["alarm_high_high"].get<double>())
                                         : SqlArgNull(),
        SqlArg(body.value("sample_interval", 0)),
        body.contains("is_key_metric") ? SqlArg(body["is_key_metric"].get<bool>()) : SqlArgNull(),
        SqlArg(body.value("status", -1)));
}

// 传感器软删: 关键指标 (OEE run_status 依赖) 或近 24h 仍有采集数据 -> 409 拒绝
void deleteSensor(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT s.is_key_metric, "
        "EXISTS(SELECT 1 FROM iot_raw_data r WHERE r.sensor_id = s.id "
        "       AND r.collected_at > NOW() - INTERVAL '24 hours') AS has_recent_data "
        "FROM iot_sensors s WHERE s.id = $1 AND s.deleted = FALSE",
        [id, onOk, onErr](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr); // controller 转 404
            if (r[0]["is_key_metric"].as<bool>())
                return onErr(409, "关键指标传感器 (is_key_metric) 被 OEE 计算引用, 禁止删除");
            if (r[0]["has_recent_data"].as<bool>())
                return onErr(409, "传感器近 24 小时仍有采集数据, 禁止删除");
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "UPDATE iot_sensors SET deleted = TRUE, status = 0 WHERE id = $1 RETURNING id",
                [id, onOk](const drogon::orm::Result& rr) {
                    onOk(rr.empty() ? nlohmann::json(nullptr) : nlohmann::json{{"id", id}});
                },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
                SqlArg(id));
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id));
}

// ============ 采集数据 ============

void realtimeData(int64_t deviceId, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    // 每传感器最新一条 (idx_raw_device_time 支持, 限 24h 窗口避免全表扫)
    db->execSqlAsync(
        "SELECT DISTINCT ON (r.sensor_id) s.sensor_code, s.sensor_name, s.unit, r.value_num, "
        "r.value_str, r.quality, r.collected_at "
        "FROM iot_raw_data r JOIN iot_sensors s ON s.id = r.sensor_id AND s.deleted = FALSE "
        "WHERE r.device_id = $1 AND r.collected_at > NOW() - INTERVAL '24 hours' "
        "ORDER BY r.sensor_id, r.collected_at DESC",
        [deviceId, onOk](const drogon::orm::Result& r) {
            nlohmann::json points = nlohmann::json::array();
            for (const auto& row : r) {
                points.push_back({{"sensor_code", row["sensor_code"].as<std::string>()},
                                  {"sensor_name", row["sensor_name"].as<std::string>()},
                                  {"unit", optStr(row, "unit")},
                                  {"value", row["value_num"].isNull()
                                                ? nlohmann::json(optStr(row, "value_str"))
                                                : nlohmann::json(row["value_num"].as<double>())},
                                  {"quality", row["quality"].as<int>()},
                                  {"collected_at", optStr(row, "collected_at")}});
            }
            onOk({{"device_id", deviceId}, {"points", points}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(deviceId));
}

void sensorHistory(int64_t sensorId, const std::string& startTime, const std::string& endTime,
                   const std::string& interval, const std::string& agg, JsonCb onOk, ErrCb onErr) {
    int64_t step = 0;
    if (!intervalToSeconds(interval, step))
        return onErr(400, "interval 仅支持 1m/5m/15m/30m/1h/6h/1d");
    // start_time/end_time 缺省: [now-1h, now], 便于大屏/调试直接查询 (SQL 内 COALESCE 处理)
    std::string aggFn = (agg == "min" || agg == "max") ? agg : "avg";

    auto db = drogon::app().getDbClient();
    // 时间桶聚合 + 区间统计两条查询; 桶秒数作为参数避免拼接
    db->execSqlAsync(
        "SELECT s.sensor_name, s.unit FROM iot_sensors s "
        "WHERE s.id = $1 AND s.deleted = FALSE",
        [sensorId, startTime, endTime, step, aggFn, onOk, onErr](const drogon::orm::Result& meta) {
            if (meta.empty())
                return onOk(nullptr);
            auto sensorName = meta[0]["sensor_name"].as<std::string>();
            auto unit = optStr(meta[0], "unit");
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "SELECT to_timestamp(floor(extract(epoch FROM collected_at) / $3) * $3) AS "
                "bucket, " +
                    aggFn +
                    "(value_num) AS val, COUNT(*) AS cnt "
                    "FROM iot_raw_data WHERE sensor_id = $1 "
                    "AND collected_at >= COALESCE(NULLIF($2,'')::timestamptz, NOW() - INTERVAL '1 "
                    "hour') "
                    "AND collected_at < COALESCE(NULLIF($4,'')::timestamptz, NOW()) "
                    "AND value_num IS NOT NULL "
                    "GROUP BY bucket ORDER BY bucket",
                [sensorId, sensorName, unit, startTime, endTime, onOk,
                 onErr](const drogon::orm::Result& r) {
                    nlohmann::json points = nlohmann::json::array();
                    double mn = 0, mx = 0, sum = 0;
                    int64_t cnt = 0;
                    for (const auto& row : r) {
                        auto v = row["val"].as<double>();
                        points.push_back({{"time", optStr(row, "bucket")}, {"value", v}});
                        if (cnt == 0 || v < mn)
                            mn = v;
                        if (cnt == 0 || v > mx)
                            mx = v;
                        sum += v;
                        ++cnt;
                    }
                    nlohmann::json stats =
                        cnt > 0 ? nlohmann::json{{"min", mn},
                                                 {"max", mx},
                                                 {"avg", sum / static_cast<double>(cnt)},
                                                 {"count", cnt}}
                                : nlohmann::json(nullptr);
                    onOk({{"sensor_id", sensorId},
                          {"sensor_name", sensorName},
                          {"unit", unit},
                          {"points", points},
                          {"stats", stats}});
                },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
                SqlArg(sensorId), startTime, SqlArg(step), endTime);
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(sensorId));
}

// ============ 告警 ============

void listAlerts(int page, int pageSize, int status, int level, int64_t deviceId, JsonCb onOk,
                ErrCb onErr) {
    clampPage(page, pageSize);
    std::string where = "WHERE 1=1";
    if (status >= 0)
        where += " AND a.status = " + std::to_string(status);
    if (level > 0)
        where += " AND a.alert_level = " + std::to_string(level);
    if (deviceId > 0)
        where += " AND a.device_id = " + std::to_string(deviceId);

    std::string listSql =
        "SELECT a.id, a.device_id, COALESCE(d.device_name,'') AS device_name, a.sensor_id, "
        "a.alert_type, a.alert_level, a.alert_value, a.threshold, a.message, a.status, "
        "a.acknowledged_by, a.acknowledged_at, a.created_at "
        "FROM iot_alerts a LEFT JOIN iot_devices d ON d.id = a.device_id " +
        where + " ORDER BY a.id DESC LIMIT " + std::to_string(pageSize) + " OFFSET " +
        std::to_string((page - 1) * pageSize);
    std::string countSql = "SELECT COUNT(*) AS cnt FROM iot_alerts a " + where;

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        listSql,
        [page, pageSize, countSql, onOk, onErr](const drogon::orm::Result& r) {
            nlohmann::json listArr = nlohmann::json::array();
            for (const auto& row : r) {
                listArr.push_back(
                    {{"id", row["id"].as<int64_t>()},
                     {"device_id", row["device_id"].as<int64_t>()},
                     {"device_name", row["device_name"].as<std::string>()},
                     {"sensor_id", row["sensor_id"].isNull() ? 0 : row["sensor_id"].as<int64_t>()},
                     {"alert_type", row["alert_type"].as<std::string>()},
                     {"alert_level", row["alert_level"].as<int>()},
                     {"alert_value", row["alert_value"].isNull()
                                         ? nlohmann::json(nullptr)
                                         : nlohmann::json(row["alert_value"].as<double>())},
                     {"threshold", row["threshold"].isNull()
                                       ? nlohmann::json(nullptr)
                                       : nlohmann::json(row["threshold"].as<double>())},
                     {"message", optStr(row, "message")},
                     {"status", row["status"].as<int>()},
                     {"acknowledged_by", row["acknowledged_by"].isNull()
                                             ? nlohmann::json(nullptr)
                                             : nlohmann::json(row["acknowledged_by"].as<int64_t>())},
                     {"acknowledged_at", optStr(row, "acknowledged_at")},
                     {"created_at", optStr(row, "created_at")}});
            }
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                countSql,
                [listArr, page, pageSize, onOk](const drogon::orm::Result& cr) {
                    onOk({{"list", listArr},
                          {"total", cr.empty() ? 0 : cr[0]["cnt"].as<int64_t>()},
                          {"page", page},
                          {"page_size", pageSize}});
                },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
}

void acknowledgeAlert(int64_t id, int64_t userId, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_alerts SET status = 1, acknowledged_by = $2, acknowledged_at = NOW() "
        "WHERE id = $1 AND status = 0 RETURNING id",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr); // 不存在或已被处理 -> 404
            onOk({{"id", id}, {"status", 1}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id), SqlArg(userId));
}

// 告警消除: 已确认(1) -> 已消除(2), 记录 resolved_at; 操作人补 acknowledged_by (幂等保留首人)
void resolveAlert(int64_t id, int64_t userId, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_alerts SET status = 2, resolved_at = NOW(), "
        "acknowledged_by = COALESCE(acknowledged_by, $2), "
        "acknowledged_at = COALESCE(acknowledged_at, NOW()) "
        "WHERE id = $1 AND status = 1 RETURNING id",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr); // 仅已确认状态可消除 -> 404
            onOk({{"id", id}, {"status", 2}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id), SqlArg(userId));
}

// 告警忽略: 未处理(0)/已确认(1) -> 已忽略(3); 误报告警快速关闭通道
void dismissAlert(int64_t id, int64_t userId, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_alerts SET status = 3, "
        "acknowledged_by = COALESCE(acknowledged_by, $2), "
        "acknowledged_at = COALESCE(acknowledged_at, NOW()) "
        "WHERE id = $1 AND status IN (0, 1) RETURNING id",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr); // 已消除/已忽略不可再操作 -> 404
            onOk({{"id", id}, {"status", 3}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id), SqlArg(userId));
}

// ============ 指令下发 ============

void sendCommand(int64_t deviceId, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto command = body.value("command", "");
    if (command.empty())
        return onErr(400, "command 必填");

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT device_code FROM iot_devices WHERE id = $1 AND deleted = FALSE",
        [deviceId, command, body, onOk, onErr](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr);
            auto deviceCode = r[0]["device_code"].as<std::string>();
            nlohmann::json msg;
            msg["version"] = "1.0";
            msg["type"] = "device_command";
            msg["device_id"] = deviceId;
            msg["device_code"] = deviceCode;
            msg["command"] = command;
            msg["params"] = body.value("params", nlohmann::json::object());
            msg["timestamp"] = TimeUtils::nowUtcIso();
            // outbox 事务投递: 落库与 MQ 原子 (全项目唯一 MQ 入口约定)
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                OutboxService::kEnqueueSql,
                [deviceId, onOk](const drogon::orm::Result&) {
                    onOk({{"device_id", deviceId}, {"queued", true}});
                },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
                "iot.exchange", "cmd.dev." + std::to_string(deviceId), msg.dump());
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(deviceId));
}

// ============ 采集任务 ============

void listTasks(int page, int pageSize, JsonCb onOk, ErrCb onErr) {
    clampPage(page, pageSize);
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT t.id, t.task_code, t.task_name, t.protocol, t.schedule_type, t.interval_ms, "
        "t.config, t.enabled, t.last_run_at, t.next_run_at, t.created_at, "
        "(SELECT COALESCE(json_agg(td.device_id), '[]'::json) FROM iot_task_devices td "
        " WHERE td.task_id = t.id) AS device_ids "
        "FROM iot_collection_tasks t ORDER BY t.id DESC LIMIT " +
            std::to_string(pageSize) + " OFFSET " + std::to_string((page - 1) * pageSize),
        [page, pageSize, onOk, onErr](const drogon::orm::Result& r) {
            nlohmann::json listArr = nlohmann::json::array();
            for (const auto& row : r) {
                listArr.push_back({{"id", row["id"].as<int64_t>()},
                                   {"task_code", row["task_code"].as<std::string>()},
                                   {"task_name", row["task_name"].as<std::string>()},
                                   {"protocol", row["protocol"].as<std::string>()},
                                   {"schedule_type", row["schedule_type"].as<int>()},
                                   {"interval_ms", row["interval_ms"].as<int>()},
                                   {"config", parseJsonField(optStr(row, "config"))},
                                   {"enabled", row["enabled"].as<bool>()},
                                   {"device_ids", parseJsonField(optStr(row, "device_ids"))},
                                   {"created_at", optStr(row, "created_at")}});
            }
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "SELECT COUNT(*) AS cnt FROM iot_collection_tasks",
                [listArr, page, pageSize, onOk](const drogon::orm::Result& cr) {
                    onOk({{"list", listArr},
                          {"total", cr.empty() ? 0 : cr[0]["cnt"].as<int64_t>()},
                          {"page", page},
                          {"page_size", pageSize}});
                },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
}

// 任务主体落库后挂接设备关联 (device_ids 数组); 关联失败仅日志 (主体已建)
void createTask(const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto code = body.value("task_code", "");
    auto name = body.value("task_name", "");
    auto protocol = body.value("protocol", "");
    if (code.empty() || name.empty() || protocol.empty())
        return onErr(400, "task_code/task_name/protocol 必填");

    std::vector<int64_t> deviceIds;
    if (body.contains("device_ids") && body["device_ids"].is_array())
        for (const auto& v : body["device_ids"])
            deviceIds.push_back(v.get<int64_t>());

    auto db = drogon::app().getDbClient();
    auto cfg = body.contains("config") ? body["config"].dump() : "{}";
    db->execSqlAsync(
        "INSERT INTO iot_collection_tasks (task_code, task_name, protocol, schedule_type, "
        "interval_ms, config, enabled) VALUES ($1,$2,$3,$4,$5,$6::jsonb,$7) RETURNING id",
        [deviceIds, onOk](const drogon::orm::Result& r) {
            auto taskId = r[0]["id"].as<int64_t>();
            if (deviceIds.empty())
                return onOk({{"id", taskId}});
            auto db2 = drogon::app().getDbClient();
            auto remaining = std::make_shared<int>(static_cast<int>(deviceIds.size()));
            for (auto devId : deviceIds) {
                db2->execSqlAsync(
                    "INSERT INTO iot_task_devices (task_id, device_id) VALUES ($1,$2) "
                    "ON CONFLICT DO NOTHING",
                    [taskId, remaining, onOk](const drogon::orm::Result&) {
                        if (--(*remaining) == 0)
                            onOk({{"id", taskId}});
                    },
                    [](const drogon::orm::DrogonDbException& e) {
                        LOG_ERROR << "task device link failed: " << e.base().what();
                    },
                    SqlArg(taskId), SqlArg(devId));
            }
        },
        [onErr](const drogon::orm::DrogonDbException& e) {
            onErr(errCodeOf(e) == 409 ? 409 : 500,
                  errCodeOf(e) == 409 ? "任务编码已存在" : e.base().what());
        },
        code, name, protocol, SqlArg(body.value("schedule_type", 1)),
        SqlArg(body.value("interval_ms", 1000)), cfg, SqlArg(body.value("enabled", true)));
}

void updateTask(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_collection_tasks SET "
        "task_name = COALESCE(NULLIF($2,''), task_name), "
        "schedule_type = CASE WHEN $3 > 0 THEN $3::smallint ELSE schedule_type END, "
        "interval_ms = CASE WHEN $4 > 0 THEN $4 ELSE interval_ms END, "
        "config = CASE WHEN $5::jsonb <> '{}'::jsonb THEN $5::jsonb ELSE config END, "
        "updated_at = NOW() "
        "WHERE id = $1 RETURNING id",
        [id, onOk](const drogon::orm::Result& r) {
            onOk(r.empty() ? nlohmann::json(nullptr) : nlohmann::json{{"id", id}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id), body.value("task_name", ""), SqlArg(body.value("schedule_type", 0)),
        SqlArg(body.value("interval_ms", 0)),
        body.contains("config") ? body["config"].dump() : "{}");
}

void deleteTask(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "DELETE FROM iot_collection_tasks WHERE id = $1 RETURNING id",
        [id, onOk](const drogon::orm::Result& r) {
            onOk(r.empty() ? nlohmann::json(nullptr) : nlohmann::json{{"id", id}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id));
}

void toggleTask(int64_t id, bool enabled, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE iot_collection_tasks SET enabled = $2, updated_at = NOW() "
        "WHERE id = $1 RETURNING id",
        [id, enabled, onOk](const drogon::orm::Result& r) {
            onOk(r.empty() ? nlohmann::json(nullptr)
                           : nlohmann::json{{"id", id}, {"enabled", enabled}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id), SqlArg(enabled));
}

} // namespace hms::IotService
