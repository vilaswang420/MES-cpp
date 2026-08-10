#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

// JWT 签发与校验 (设计文档 5.2 节)。HS256; payload 含合成后的
// data_scope 与 custom_dept_ids (多角色取最宽, 见 RbacService::mergeDataScope)。
namespace hms::JwtUtils {

struct TokenPayload {
    int64_t userId = 0;
    std::string username;
    int64_t deptId = 0;
    std::vector<std::string> roles;
    int dataScope = 1;
    std::vector<int64_t> customDeptIds;
    std::string sessionId;
    std::string jti;
    int64_t iat = 0;
    int64_t exp = 0;
};

// 密钥从配置注入 (生产从环境变量读取, 禁止硬编码)
void init(const std::string& secret, int accessTtlSec, int refreshTtlSec);

// 签发 access / refresh token
std::string signAccessToken(const TokenPayload& p);
std::string signRefreshToken(int64_t userId, const std::string& sessionId);

// 校验签名与过期时间; 失败返回 nullopt
std::optional<TokenPayload> verifyAccessToken(const std::string& token);
std::optional<nlohmann::json> verifyRefreshToken(const std::string& token);

} // namespace hms::JwtUtils
