// bcrypt 往返验证单测: 哈希/校验自洽 + 种子向量核对
#include <bcrypt/BCrypt.hpp>
#include <gtest/gtest.h>

#include <iostream>

TEST(BcryptTest, RoundTrip) {
    auto hash = BCrypt::generateHash("password", 10);
    std::cout << "[bcrypt] generated hash for 'password': " << hash << std::endl;
    EXPECT_TRUE(BCrypt::validatePassword("password", hash));
    EXPECT_FALSE(BCrypt::validatePassword("wrong", hash));
}

TEST(BcryptTest, SeedVector) {
    // 002_seed.up.sql 中 admin 的哈希 (本库生成), 必须对应明文 password,
    // 否则 E2E 登录全链路断裂
    const std::string seed = "$2a$10$Vq7ZaCglT1G6pU4uG9zUJ.LRt4xCFfHpI1O6xY3FQ/6IxnQHi/JN6";
    EXPECT_TRUE(BCrypt::validatePassword("password", seed));
}

TEST(BcryptTest, KnownVector) {
    // crypt_blowfish 官方测试向量 (固定 salt)
    EXPECT_TRUE(BCrypt::validatePassword(
        "U*U", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW"));
}
