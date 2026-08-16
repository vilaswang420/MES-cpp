// ConfigLoader.cc — 配置加载器实现 (P4-5.1)
// 通过 raw socket HTTP 请求后端 REST API 拉取设备+传感器配置。
// 无额外依赖 (不引 libcurl), 仅使用 POSIX socket + nlohmann/json。
#include "collector/ConfigLoader.hh"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hms::iot {

namespace {

// 简单 HTTP 客户端: GET/POST via raw socket, 返回 HTTP body
// 仅支持 IP 直连 (容器内服务间通信, 无需 DNS)
std::string httpGet(const std::string& host, int port, const std::string& path,
                    const std::string& authToken = "") {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    timeval tv{};
    tv.tv_sec = 5;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        ::close(sock);
        return "";
    }
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        return "";
    }

    // 构造 HTTP GET 请求
    std::string req = "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    if (!authToken.empty()) {
        req += "Authorization: Bearer " + authToken + "\r\n";
    }
    req += "Connection: close\r\n\r\n";

    if (::send(sock, req.data(), req.size(), 0) < 0) {
        ::close(sock);
        return "";
    }

    // 读取全部响应
    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(sock, buf, sizeof(buf), 0)) > 0) {
        response.append(buf, static_cast<size_t>(n));
    }
    ::close(sock);

    // 分离 header / body (找 \r\n\r\n)
    auto sep = response.find("\r\n\r\n");
    if (sep == std::string::npos) return "";
    return response.substr(sep + 4);
}

// POST 请求 (用于登录)
std::string httpPost(const std::string& host, int port, const std::string& path,
                     const std::string& body) {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    timeval tv{};
    tv.tv_sec = 5;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        ::close(sock);
        return "";
    }
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        return "";
    }

    std::string req = "POST " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "Connection: close\r\n\r\n";
    req += body;

    if (::send(sock, req.data(), req.size(), 0) < 0) {
        ::close(sock);
        return "";
    }

    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(sock, buf, sizeof(buf), 0)) > 0) {
        response.append(buf, static_cast<size_t>(n));
    }
    ::close(sock);

    auto sep = response.find("\r\n\r\n");
    if (sep == std::string::npos) return "";
    return response.substr(sep + 4);
}

// 从 URL "http://host:port/path" 解析出 host, port, path
struct UrlParts {
    std::string host;
    int port = 80;
    std::string path;
};

bool parseUrl(const std::string& url, UrlParts& out) {
    // 期望格式: http://host:port/path...
    if (url.rfind("http://", 0) != 0) return false;
    std::string rest = url.substr(7); // skip "http://"
    auto slashPos = rest.find('/');
    std::string hostPort = (slashPos == std::string::npos) ? rest : rest.substr(0, slashPos);
    out.path = (slashPos == std::string::npos) ? "/" : rest.substr(slashPos);

    auto colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        out.host = hostPort.substr(0, colonPos);
        out.port = std::stoi(hostPort.substr(colonPos + 1));
    } else {
        out.host = hostPort;
        out.port = 80;
    }
    return !out.host.empty();
}

} // namespace

ConfigLoadResult loadConfig(const std::string& backendUrl,
                            const std::string& username,
                            const std::string& password) {
    ConfigLoadResult result;

    UrlParts url;
    if (!parseUrl(backendUrl, url)) {
        result.error = "invalid backend_url: " + backendUrl;
        return result;
    }

    // Step 1: 登录获取 JWT token
    std::string loginBody = nlohmann::json{{"username", username}, {"password", password}}.dump();
    std::string loginResp = httpPost(url.host, url.port, "/api/v1/auth/login", loginBody);
    if (loginResp.empty()) {
        result.error = "login request failed (backend unreachable at " + backendUrl + ")";
        return result;
    }

    std::string token;
    try {
        auto j = nlohmann::json::parse(loginResp);
        if (j.value("code", 0) != 200) {
            result.error = "login failed: " + j.value("message", "unknown");
            return result;
        }
        token = j["data"].value("access_token", "");
    } catch (const std::exception& e) {
        result.error = std::string("login response parse error: ") + e.what();
        return result;
    }
    if (token.empty()) {
        result.error = "login returned empty token";
        return result;
    }

    // Step 2: 拉取设备列表 (含 sensors 子查询)
    std::string devicesResp = httpGet(url.host, url.port,
                                      "/api/v1/iot/devices?page=1&page_size=999", token);
    if (devicesResp.empty()) {
        result.error = "devices request failed";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(devicesResp);
        if (j.value("code", 0) != 200) {
            result.error = "devices fetch failed: " + j.value("message", "unknown");
            return result;
        }

        const auto& list = j["data"]["list"];
        if (!list.is_array()) {
            result.error = "devices list not array";
            return result;
        }

        for (const auto& d : list) {
            DeviceConfig dev;
            dev.device_id = d.value("id", static_cast<int64_t>(0));
            dev.device_code = d.value("device_code", "");
            dev.device_name = d.value("device_name", "");
            dev.protocol = d.value("protocol", "");
            dev.ip_address = d.value("ip_address", "");
            dev.port = d.value("port", 502);

            // unit_id 从 connection_config JSON 中读取
            if (d.contains("connection_config") && d["connection_config"].is_object()) {
                dev.unit_id = d["connection_config"].value("unit_id", 1);
                dev.poll_interval_ms = d["connection_config"].value("poll_interval_ms", 1000);
            }

            // 仅处理 modbus_tcp 协议且状态正常的设备
            if (dev.protocol != "modbus_tcp") {
                std::cout << "[config] skipping non-modbus device: " << dev.device_code
                          << " (protocol=" << dev.protocol << ")\n";
                continue;
            }
            if (dev.ip_address.empty() || dev.port == 0) {
                std::cerr << "[config] skipping device with no IP/port: " << dev.device_code << "\n";
                continue;
            }

            // 校验 device_id
            if (dev.device_id <= 0) {
                result.error = "invalid device_id for " + dev.device_code;
                return result;
            }

            // 解析传感器
            if (d.contains("sensors") && d["sensors"].is_array()) {
                for (const auto& s : d["sensors"]) {
                    SensorConfig sensor;
                    sensor.sensor_id = s.value("id", static_cast<int64_t>(0));
                    sensor.sensor_code = s.value("sensor_code", "");
                    sensor.sensor_name = s.value("sensor_name", "");
                    sensor.data_type = s.value("data_type", "uint16");
                    sensor.unit = s.value("unit", "");
                    sensor.scale_factor = s.value("scale_factor", 1.0);
                    sensor.sample_interval = s.value("sample_interval", 1000);

                    // register_addr 从字符串解析为整数 (如 "40001" → 40001)
                    std::string addrStr = s.value("register_addr", "");
                    if (!addrStr.empty()) {
                        sensor.register_addr = std::stoi(addrStr);
                    }

                    // 校验 sensor_id
                    if (sensor.sensor_id <= 0) {
                        result.error = "invalid sensor_id for " + sensor.sensor_code;
                        return result;
                    }
                    if (sensor.register_addr <= 0) {
                        std::cerr << "[config] skipping sensor with no register_addr: "
                                  << sensor.sensor_code << "\n";
                        continue;
                    }

                    dev.sensors.push_back(std::move(sensor));
                }
            }

            if (dev.sensors.empty()) {
                std::cerr << "[config] device " << dev.device_code
                          << " has no valid sensors, skipping\n";
                continue;
            }

            std::cout << "[config] loaded device " << dev.device_code
                      << " (id=" << dev.device_id << ", " << dev.ip_address << ":"
                      << dev.port << ", unit_id=" << dev.unit_id << ", sensors="
                      << dev.sensors.size() << ")\n";

            result.devices.push_back(std::move(dev));
        }
    } catch (const std::exception& e) {
        result.error = std::string("devices response parse error: ") + e.what();
        return result;
    }

    if (result.devices.empty()) {
        result.error = "no valid modbus_tcp devices configured";
        return result;
    }

    result.ok = true;
    std::cout << "[config] loaded " << result.devices.size() << " device(s) from backend\n";
    return result;
}

} // namespace hms::iot
