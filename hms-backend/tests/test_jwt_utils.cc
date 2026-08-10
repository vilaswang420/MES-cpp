#include <gtest/gtest.h>

#include "utils/JwtUtils.hh"

// JWT 签发/校验单测 (设计文档 5.2 节)。
// 覆盖: 往返一致性 / 篡改拒绝 / 密钥错误拒绝 / access-refresh 类型隔离。
using namespace hms::JwtUtils;

namespace {
TokenPayload makePayload() {
    TokenPayload p;
    p.userId = 42;
    p.username = "operator1";
    p.deptId = 7;
    p.roles = {"operator", "dashboard"};
    p.dataScope = 3;
    p.customDeptIds = {};
    p.sessionId = "sess-abc";
    p.jti = "jti-xyz";
    return p;
}
} // namespace

TEST(JwtUtils, AccessTokenRoundTrip) {
    init("unit-test-secret", 3600, 86400);
    auto token = signAccessToken(makePayload());
    auto claims = verifyAccessToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->userId, 42);
    EXPECT_EQ(claims->username, "operator1");
    EXPECT_EQ(claims->deptId, 7);
    ASSERT_EQ(claims->roles.size(), 2u);
    EXPECT_EQ(claims->roles[0], "operator");
    EXPECT_EQ(claims->dataScope, 3);
    EXPECT_EQ(claims->sessionId, "sess-abc");
    EXPECT_EQ(claims->jti, "jti-xyz");
    EXPECT_GT(claims->exp, claims->iat);
}

TEST(JwtUtils, CustomDeptIdsRoundTrip) {
    init("unit-test-secret", 3600, 86400);
    auto p = makePayload();
    p.dataScope = 5;
    p.customDeptIds = {11, 22, 33};
    auto claims = verifyAccessToken(signAccessToken(p));
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->dataScope, 5);
    ASSERT_EQ(claims->customDeptIds.size(), 3u);
    EXPECT_EQ(claims->customDeptIds[2], 33);
}

TEST(JwtUtils, TamperedTokenRejected) {
    init("unit-test-secret", 3600, 86400);
    auto token = signAccessToken(makePayload());
    // 篡改 payload 段任一字符 -> 签名校验失败
    auto pos = token.find('.', token.find('.') + 1);
    ASSERT_NE(pos, std::string::npos);
    if (pos + 2 < token.size())
        token[pos + 2] = token[pos + 2] == 'A' ? 'B' : 'A';
    EXPECT_FALSE(verifyAccessToken(token).has_value());
}

TEST(JwtUtils, WrongSecretRejected) {
    init("unit-test-secret", 3600, 86400);
    auto token = signAccessToken(makePayload());
    init("another-secret", 3600, 86400); // 模拟密钥不匹配 (如实例间配置漂移)
    EXPECT_FALSE(verifyAccessToken(token).has_value());
    init("unit-test-secret", 3600, 86400); // 还原
}

TEST(JwtUtils, TokenTypeIsolation) {
    init("unit-test-secret", 3600, 86400);
    auto refresh = signRefreshToken(42, "sess-abc");
    // refresh token 不能被当作 access token 使用 (反之亦然)
    EXPECT_FALSE(verifyAccessToken(refresh).has_value());

    auto access = signAccessToken(makePayload());
    EXPECT_FALSE(verifyRefreshToken(access).has_value());

    auto claims = verifyRefreshToken(refresh);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ((*claims)["user_id"].get<int64_t>(), 42);
    EXPECT_EQ((*claims)["session_id"].get<std::string>(), "sess-abc");
}

TEST(JwtUtils, GarbageTokenRejected) {
    init("unit-test-secret", 3600, 86400);
    EXPECT_FALSE(verifyAccessToken("not-a-jwt").has_value());
    EXPECT_FALSE(verifyAccessToken("").has_value());
    EXPECT_FALSE(verifyRefreshToken("a.b.c").has_value());
}
