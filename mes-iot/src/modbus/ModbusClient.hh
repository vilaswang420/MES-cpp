// ModbusClient.hh — Modbus TCP 协议层 (P4-5.1)
// 自实现 MBAP 帧编解码 + FC=0x03 读保持寄存器, 不引 libmodbus (~300 行)。
// 协议参考: MODBUS Messaging on TCP/IP Implementation Guide v1.0b
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mes::iot {

// Modbus 异常码
enum class ModbusError : int {
    Ok = 0,
    IllegalFunction = 1,
    IllegalDataAddress = 2,
    IllegalDataValue = 3,
    ServerDeviceFailure = 4,
    Acknowledge = 5,
    ServerDeviceBusy = 6,
    GatewayPathUnavailable = 10,
    GatewayTargetNoResponse = 11,
    SocketError = -1,
    Timeout = -2,
    TruncatedFrame = -3,
    TransactionMismatch = -4,
};

// 单次读保持寄存器结果
struct ReadResult {
    ModbusError error = ModbusError::Ok;
    std::vector<uint16_t> registers; // 读到的寄存器值 (大端序已转换)
};

// Modbus TCP 客户端: 单设备连接, 线程不安全 (每设备独立实例, 由 DevicePoller 单线程调用)
class ModbusClient {
public:
    ModbusClient(std::string host, int port, int unitId = 1);
    ~ModbusClient();

    // 连接设备 (阻塞, 成功返回 true)
    bool connect();

    // 断开连接
    void disconnect();

    // 是否已连接
    bool isConnected() const;

    // 读保持寄存器 (FC=0x03)
    // startAddr: 协议偏移 (40001 → 0)
    // quantity: 寄存器数量 (≤125)
    ReadResult readHoldingRegisters(uint16_t startAddr, uint16_t quantity);

    // 获取主机信息 (用于日志)
    const std::string& host() const { return host_; }
    int port() const { return port_; }
    int unitId() const { return unitId_; }

private:
    // 发送请求并等待响应 (带 transaction_id 匹配)
    ReadResult sendAndReceive(const std::vector<uint8_t>& request, uint16_t expectedDataLen);

    // 接收 N 字节 (阻塞, 带超时)
    bool recvN(void* buf, size_t n, int timeoutMs);

    std::string host_;
    int port_;
    int unitId_;
    int sock_; // socket fd, -1 = 未连接
    uint16_t nextTransactionId_;
};

} // namespace mes::iot
