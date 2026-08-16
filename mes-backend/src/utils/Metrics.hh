#pragma once

// Prometheus 指标注册表 (计划任务 28, header-only):
// counter/gauge/histogram 三类, 线程安全; /metrics 端点渲染 Prometheus 文本格式。
// 埋点约定: HTTP 指标走 CrossCutting preSendingAdvice; 业务指标由各模块直接调用。
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace mes::Metrics {

namespace detail {

inline std::mutex& mu() {
    static std::mutex m;
    return m;
}

// 带标签 key 的计数器: key = name{labels}
inline std::map<std::string, uint64_t>& counters() {
    static std::map<std::string, uint64_t> m;
    return m;
}

inline std::map<std::string, double>& gauges() {
    static std::map<std::string, double> m;
    return m;
}

// 直方图: 固定桶 (毫秒), 延迟分布门禁参考 m1 基线 P95≈300ms
constexpr std::array<double, 9> kBuckets = {10, 25, 50, 100, 200, 300, 500, 1000, 2000};

struct Histogram {
    std::array<uint64_t, kBuckets.size()> buckets{};
    uint64_t count = 0;
    double sum = 0;
};

inline std::map<std::string, Histogram>& histograms() {
    static std::map<std::string, Histogram> m;
    return m;
}

} // namespace detail

inline void counterInc(const std::string& key, uint64_t delta = 1) {
    std::lock_guard lk(detail::mu());
    detail::counters()[key] += delta;
}

inline void gaugeSet(const std::string& key, double value) {
    std::lock_guard lk(detail::mu());
    detail::gauges()[key] = value;
}

inline void histogramObserve(const std::string& name, double value) {
    std::lock_guard lk(detail::mu());
    auto& h = detail::histograms()[name];
    for (size_t i = 0; i < detail::kBuckets.size(); ++i)
        if (value <= detail::kBuckets[i])
            ++h.buckets[i];
    ++h.count;
    h.sum += value;
}

// Prometheus 文本格式渲染 (text/plain; version=0.0.4)
inline std::string render() {
    std::lock_guard lk(detail::mu());
    std::ostringstream out;

    // counter: 从 key 反推指标名 ({ 前缀) 归组 TYPE 行
    std::map<std::string, bool> counterNames;
    for (const auto& [key, v] : detail::counters()) {
        auto name = key.substr(0, key.find('{'));
        if (!counterNames[name]) {
            counterNames[name] = true;
            out << "# TYPE " << name << " counter\n";
        }
        out << key << ' ' << v << '\n';
    }

    std::map<std::string, bool> gaugeNames;
    for (const auto& [key, v] : detail::gauges()) {
        auto name = key.substr(0, key.find('{'));
        if (!gaugeNames[name]) {
            gaugeNames[name] = true;
            out << "# TYPE " << name << " gauge\n";
        }
        out << key << ' ' << v << '\n';
    }

    for (const auto& [name, h] : detail::histograms()) {
        out << "# TYPE " << name << " histogram\n";
        // observe 时每次观测对所有满足 value<=边界 的桶 +1, 桶值即累计数
        for (size_t i = 0; i < detail::kBuckets.size(); ++i) {
            out << name << "_bucket{le=\"" << detail::kBuckets[i] << "\"} " << h.buckets[i] << '\n';
        }
        out << name << "_bucket{le=\"+Inf\"} " << h.count << '\n';
        out << name << "_sum " << h.sum << '\n';
        out << name << "_count " << h.count << '\n';
    }
    return out.str();
}

} // namespace mes::Metrics
