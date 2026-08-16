// ModbusClient.cc — Modbus TCP 协议层实现 (P4-5.1)
// MBAP 帧: [transaction_id:2B] [protocol_id:2B=0] [length:2B] [unit_id:1B] + PDU
// FC=0x03 请求 PDU: [0x03] [start_addr:2B] [quantity:2B]
// FC=0x03 响应 PDU: [0x03] [byte_count:1B] [data:N*2B]
// 异常响应 PDU: [0x80|FC] [exception_code:1B]
#include "modbus/ModbusClient.hh"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>

namespace mes::iot {

namespace {

// MBAP + PDU 常量
constexpr uint16_t kProtocolId = 0;
constexpr uint8_t kFcReadHolding = 0x03;
constexpr uint8_t kExceptionBit = 0x80;
constexpr int kRecvTimeoutMs = 3000; // 单次读超时 3s

// 大端序写入
void writeU16BE(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>(val >> 8));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

uint16_t readU16BE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) << 8 | p[1];
}

} // namespace

ModbusClient::ModbusClient(std::string host, int port, int unitId)
    : host_(std::move(host)), port_(port), unitId_(unitId), sock_(-1),
      nextTransactionId_(1) {}

ModbusClient::~ModbusClient() {
    disconnect();
}

bool ModbusClient::connect() {
    if (sock_ >= 0) return true;

    sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) {
        std::cerr << "[modbus] socket() failed: " << std::strerror(errno) << "\n";
        return false;
    }

    // 设置发送/接收超时
    timeval tv{};
    tv.tv_sec = kRecvTimeoutMs / 1000;
    tv.tv_usec = (kRecvTimeoutMs % 1000) * 1000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        // 非 IP 地址 → DNS 解析 (简化: 仅支持 IP, 不支持域名; 容器内用 IP)
        std::cerr << "[modbus] invalid IP: " << host_ << " (use IP not hostname)\n";
        ::close(sock_);
        sock_ = -1;
        return false;
    }

    if (::connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[modbus] connect " << host_ << ":" << port_ << " failed: "
                  << std::strerror(errno) << "\n";
        ::close(sock_);
        sock_ = -1;
        return false;
    }

    std::cout << "[modbus] connected to " << host_ << ":" << port_
              << " unit_id=" << unitId_ << "\n";
    return true;
}

void ModbusClient::disconnect() {
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
}

bool ModbusClient::isConnected() const {
    return sock_ >= 0;
}

ReadResult ModbusClient::readHoldingRegisters(uint16_t startAddr, uint16_t quantity) {
    ReadResult result;

    if (quantity == 0 || quantity > 125) {
        result.error = ModbusError::IllegalDataValue;
        return result;
    }
    if (sock_ < 0) {
        result.error = ModbusError::SocketError;
        return result;
    }

    // 构造请求 PDU: FC(1) + start_addr(2) + quantity(2) = 5 bytes
    // MBAP: transaction(2) + protocol(2) + length(2) + unit_id(1) = 7 bytes
    // length = unit_id(1) + PDU(5) = 6
    std::vector<uint8_t> req;
    uint16_t txnId = nextTransactionId_++;
    writeU16BE(req, txnId);        // transaction_id
    writeU16BE(req, kProtocolId);  // protocol_id = 0
    writeU16BE(req, 6);            // length = 6
    req.push_back(static_cast<uint8_t>(unitId_)); // unit_id
    req.push_back(kFcReadHolding); // function code
    writeU16BE(req, startAddr);    // start address
    writeU16BE(req, quantity);     // quantity

    // 发送
    ssize_t sent = ::send(sock_, req.data(), req.size(), 0);
    if (sent < 0 || static_cast<size_t>(sent) != req.size()) {
        result.error = ModbusError::SocketError;
        disconnect();
        return result;
    }

    // 接收 MBAP 头 (7 bytes)
    uint8_t mbap[7];
    if (!recvN(mbap, 7, kRecvTimeoutMs)) {
        result.error = ModbusError::Timeout;
        disconnect();
        return result;
    }

    uint16_t respTxn = readU16BE(mbap);
    uint16_t respProto = readU16BE(mbap + 2);
    uint16_t respLen = readU16BE(mbap + 4);
    uint8_t respUnitId = mbap[6];

    // 校验 transaction_id 匹配
    if (respTxn != txnId) {
        std::cerr << "[modbus] transaction mismatch: expected=" << txnId
                  << " got=" << respTxn << "\n";
        result.error = ModbusError::TransactionMismatch;
        return result;
    }
    if (respProto != kProtocolId || respUnitId != unitId_) {
        std::cerr << "[modbus] protocol/unit mismatch\n";
        result.error = ModbusError::TruncatedFrame;
        return result;
    }
    if (respLen < 2) {
        result.error = ModbusError::TruncatedFrame;
        return result;
    }

    // 接收 PDU (respLen - 1 bytes, 已扣 unit_id)
    size_t pduLen = respLen - 1;
    std::vector<uint8_t> pdu(pduLen);
    if (!recvN(pdu.data(), pduLen, kRecvTimeoutMs)) {
        result.error = ModbusError::Timeout;
        disconnect();
        return result;
    }

    // 检查异常响应
    if (pdu[0] & kExceptionBit) {
        uint8_t excCode = pduLen > 1 ? pdu[1] : 0;
        std::cerr << "[modbus] exception response: fc=" << std::hex << static_cast<int>(pdu[0])
                  << " code=" << static_cast<int>(excCode) << std::dec << "\n";
        result.error = static_cast<ModbusError>(excCode);
        return result;
    }

    // 正常响应: [0x03] [byte_count] [data...]
    if (pdu[0] != kFcReadHolding || pduLen < 2) {
        result.error = ModbusError::TruncatedFrame;
        return result;
    }

    uint8_t byteCount = pdu[1];
    size_t regCount = byteCount / 2;
    if (regCount != quantity || pduLen != 2 + byteCount) {
        std::cerr << "[modbus] response length mismatch: expected " << quantity
                  << " regs, got " << regCount << "\n";
        result.error = ModbusError::TruncatedFrame;
        return result;
    }

    // 提取寄存器值 (大端序 → host)
    result.registers.reserve(regCount);
    for (size_t i = 0; i < regCount; ++i) {
        result.registers.push_back(readU16BE(pdu.data() + 2 + i * 2));
    }

    return result;
}

bool ModbusClient::recvN(void* buf, size_t n, int timeoutMs) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t received = 0;
    while (received < n) {
        ssize_t r = ::recv(sock_, p + received, n - received, 0);
        if (r <= 0) {
            if (r == 0) return false; // 连接关闭
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 超时
                return false;
            }
            return false; // 其他错误
        }
        received += static_cast<size_t>(r);
    }
    return true;
}

} // namespace mes::iot
