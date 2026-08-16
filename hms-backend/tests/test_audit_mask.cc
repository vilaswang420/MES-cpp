// P3-4.5 审计脱敏单测: maskSensitiveJson 对密码字段掩码, 其余字段/非 JSON 原样
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "utils/AuditMask.hh"

using hms::AuditMask::maskSensitiveJson;

TEST(AuditMaskTest, LoginBodyPasswordMasked) {
    const std::string body = R"({"username":"admin","password":"S3cretP@ss"})";
    auto out = maskSensitiveJson(body);
    EXPECT_EQ(out.find("S3cretP@ss"), std::string::npos) << "明文密码不应出现在审计参数";
    EXPECT_NE(out.find("***"), std::string::npos);
    EXPECT_NE(out.find("admin"), std::string::npos) << "非敏感字段应保留";
}

TEST(AuditMaskTest, ChangePasswordBodyMasked) {
    const std::string body =
        R"({"oldPassword":"OldPass123","newPassword":"NewPass456","confirmPassword":"NewPass456"})";
    auto out = maskSensitiveJson(body);
    for (const auto* secret : {"OldPass123", "NewPass456"}) {
        EXPECT_EQ(out.find(secret), std::string::npos) << "不应泄露: " << secret;
    }
    EXPECT_NE(out.find("***"), std::string::npos);
}

TEST(AuditMaskTest, NestedObjectRecursivelyMasked) {
    const std::string body = R"({"user":{"login":"bob","password":"NestedSec"}}";
    auto out = maskSensitiveJson(body);
    EXPECT_EQ(out.find("NestedSec"), std::string::npos) << "嵌套对象密码字段应脱敏";
    EXPECT_NE(out.find("bob"), std::string::npos);
}

TEST(AuditMaskTest, NonJsonBodyPassThrough) {
    const std::string body = "raw form body password=abc123";
    EXPECT_EQ(maskSensitiveJson(body), body) << "非 JSON body 原样保留 (审计不阻断)";
}

TEST(AuditMaskTest, MalformedJsonPassThrough) {
    const std::string body = R"({"password": "unclosed)";
    EXPECT_EQ(maskSensitiveJson(body), body) << "损坏 JSON 原样保留 (fail-open)";
}

TEST(AuditMaskTest, EmptyBodyPassThrough) {
    EXPECT_EQ(maskSensitiveJson(""), "");
}
