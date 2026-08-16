#pragma once

#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

// 验证码生成/校验/渲染 (P3-4.1, 纯逻辑 header-only 可单测, 无新依赖):
// ① 生成 4 字符 (剔除易混淆 0/O/1/I/L);
// ② 大小写不敏感哈希 (统一转大写后 FNV-1a) —— Redis 只存 hash, 响应不泄露明文;
// ③ SVG 渲染 (字符随机位置/旋转/颜色 + 干扰线 + 噪点), 前端直接 data URI 展示。
namespace mes::Captcha {

// 生成随机验证码: 剔除易混淆字符 (0O1IL) 的 32 字母表
inline std::string generateCode(size_t len) {
    static const char* alphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, 31);
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i)
        s += alphabet[dist(rng) % 32];
    return s;
}

namespace detail {

// FNV-1a 64 位 (验证码仅防脚本暴力, 无需加密强度; 无 bcrypt 等新依赖)
inline uint64_t fnv1a(const char* s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<unsigned char>(s[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

inline char upper(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

inline std::string base64Encode(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 3 <= in.size()) {
        uint32_t v = (static_cast<unsigned char>(in[i]) << 16) |
                     (static_cast<unsigned char>(in[i + 1]) << 8) |
                     static_cast<unsigned char>(in[i + 2]);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    size_t rem = in.size() - i;
    if (rem == 1) {
        uint32_t v = static_cast<unsigned char>(in[i]) << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    } else if (rem == 2) {
        uint32_t v = (static_cast<unsigned char>(in[i]) << 16) |
                     (static_cast<unsigned char>(in[i + 1]) << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

} // namespace detail

// 大小写不敏感哈希: 统一转大写后 FNV-1a, hex 字符串
inline std::string hashCode(const std::string& code) {
    std::string up = code;
    for (auto& c : up)
        c = detail::upper(c);
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(detail::fnv1a(up.data(), up.size())));
    return std::string(buf);
}

// 恒定时间比较 (防时序侧信道)
inline bool constTimeEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

// 校验: 输入与存储 hash 比对 (大小写不敏感)
inline bool verifyCode(const std::string& input, const std::string& storedHash) {
    if (storedHash.empty())
        return false;
    return constTimeEq(hashCode(input), storedHash);
}

// SVG 渲染: 4 字符随机位置/旋转/颜色 + 3 干扰线 + 噪点 (无字体依赖, 用 path 描边)
inline std::string renderSvg(const std::string& code) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> posX(8.0, 22.0);
    std::uniform_real_distribution<double> posY(26.0, 34.0);
    std::uniform_real_distribution<double> rot(-18.0, 18.0);
    std::uniform_int_distribution<int> color(0, 5);
    static const char* colors[] = {"#2c5f8a", "#8a4b2c", "#2c8a4b",
                                   "#6b2c8a", "#8a2c4b", "#3a6b8a"};
    std::uniform_int_distribution<int> noise(0, 255);

    std::string svg;
    svg.reserve(1024);
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" height=\"40\" "
           "viewBox=\"0 0 120 40\">";
    svg += "<rect width=\"120\" height=\"40\" fill=\"#f5f6f8\" rx=\"4\"/>";
    // 干扰线 (3 条)
    for (int i = 0; i < 3; ++i) {
        std::uniform_real_distribution<double> x1(0.0, 30.0), y1(0.0, 40.0);
        std::uniform_real_distribution<double> x2(90.0, 120.0), y2(0.0, 40.0);
        std::uniform_int_distribution<int> alpha(40, 120);
        char line[160];
        std::snprintf(line, sizeof(line),
                      "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#9aa4b0\" "
                      "stroke-opacity=\"0.%02d\" stroke-width=\"1\"/>",
                      x1(rng), y1(rng), x2(rng), y2(rng), alpha(rng));
        svg += line;
    }
    // 字符 (独立 text, 随机位置/旋转/颜色)
    size_t i = 0;
    for (char c : code) {
        std::uniform_int_distribution<int> fs(20, 24);
        char t[200];
        std::snprintf(t, sizeof(t),
                      "<text x=\"%.1f\" y=\"%.1f\" transform=\"rotate(%.1f %d 20)\" "
                      "font-family=\"monospace\" font-size=\"%d\" font-weight=\"bold\" "
                      "fill=\"%s\">%c</text>",
                      posX(rng) + i * 26, posY(rng), rot(rng), 20 + static_cast<int>(i * 26),
                      fs(rng), colors[color(rng) % 6], c);
        svg += t;
        ++i;
    }
    // 噪点 (40 个)
    for (int n = 0; n < 40; ++n) {
        std::uniform_real_distribution<double> px(0.0, 120.0), py(0.0, 40.0);
        char dot[80];
        std::snprintf(dot, sizeof(dot),
                      "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"0.8\" fill=\"#6b7280\" "
                      "fill-opacity=\"0.%02d\"/>",
                      px(rng), py(rng), noise(rng) % 90 + 5);
        svg += dot;
    }
    svg += "</svg>";
    return svg;
}

// data URI (SVG base64): 前端 <img src> 直接展示
inline std::string svgDataUri(const std::string& code) {
    return "data:image/svg+xml;base64," + detail::base64Encode(renderSvg(code));
}

} // namespace mes::Captcha
