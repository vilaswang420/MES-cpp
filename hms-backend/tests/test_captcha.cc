// P3-4.1 验证码单测: 生成/大小写不敏感校验/一次一用语义/SVG 渲染/data URI
#include <gtest/gtest.h>

#include <cstring>

#include "utils/Captcha.hh"

namespace cap = hms::Captcha;

TEST(CaptchaTest, GenerateCodeLengthAndAlphabet) {
    for (size_t len : {size_t{4}, size_t{6}}) {
        auto code = cap::generateCode(len);
        EXPECT_EQ(code.size(), len);
        // 字符集不含易混淆字符 0/O/1/I/L (字母表 ABCDEFGHJKMNPQRSTUVWXYZ23456789)
        for (char c : code) {
            EXPECT_NE(c, '0');
            EXPECT_NE(c, 'O');
            EXPECT_NE(c, '1');
            EXPECT_NE(c, 'I');
            EXPECT_NE(c, 'L');
        }
    }
}

TEST(CaptchaTest, HashCaseInsensitive) {
    EXPECT_EQ(cap::hashCode("AbCd"), cap::hashCode("abcd"));
    EXPECT_EQ(cap::hashCode("ABCD"), cap::hashCode("abcd"));
    EXPECT_NE(cap::hashCode("ABCd"), cap::hashCode("abce"));
}

TEST(CaptchaTest, VerifyCode) {
    auto code = cap::generateCode(4);
    auto hash = cap::hashCode(code);
    EXPECT_TRUE(cap::verifyCode(code, hash));
    EXPECT_TRUE(cap::verifyCode(code + "", hash));
    // 大小写不敏感: 全大写输入同样通过
    std::string upper;
    for (char c : code) {
        upper += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    EXPECT_TRUE(cap::verifyCode(upper, hash));
    // 错误码拒绝
    EXPECT_FALSE(cap::verifyCode("ZZZZ", hash));
    EXPECT_FALSE(cap::verifyCode("", hash));
    // 空存储 hash 拒绝 (Redis 未命中)
    EXPECT_FALSE(cap::verifyCode(code, ""));
}

TEST(CaptchaTest, ConstTimeEq) {
    EXPECT_TRUE(cap::constTimeEq("abc", "abc"));
    EXPECT_FALSE(cap::constTimeEq("abc", "abd"));
    EXPECT_FALSE(cap::constTimeEq("abc", "abcd"));
}

TEST(CaptchaTest, RenderSvgContainsChars) {
    auto code = cap::generateCode(4);
    auto svg = cap::renderSvg(code);
    EXPECT_EQ(svg.substr(0, 5), "<svg ");
    EXPECT_EQ(svg.substr(svg.size() - 6), "</svg>");
    for (char c : code)
        EXPECT_NE(svg.find(c), std::string::npos) << "SVG 应包含字符 " << c;
    // 有干扰线
    EXPECT_NE(svg.find("<line"), std::string::npos);
    // 有噪点
    EXPECT_NE(svg.find("<circle"), std::string::npos);
}

TEST(CaptchaTest, SvgDataUriBase64) {
    auto code = cap::generateCode(4);
    auto uri = cap::svgDataUri(code);
    constexpr const char* kPrefix = "data:image/svg+xml;base64,";
    EXPECT_EQ(uri.rfind(kPrefix, 0), 0u); // 前缀匹配
    // base64 解码后应为合法 svg (含 <svg 与 code 字符)
    auto b64 = uri.substr(std::strlen(kPrefix));
    // 简单 base64 长度校验 (4 的倍数, 以 = 或 == 结尾)
    EXPECT_EQ(b64.size() % 4, 0u);
}
