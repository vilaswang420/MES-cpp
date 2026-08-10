#include "utils/JwtUtils.hh"

#include <jwt-cpp/jwt.h>

#include <atomic>

namespace hms::JwtUtils {

namespace {
std::string g_secret = "change-me-in-prod";
std::atomic<int> g_accessTtl{7200};
std::atomic<int> g_refreshTtl{604800};
} // namespace

void init(const std::string& secret, int accessTtlSec, int refreshTtlSec) {
    if (!secret.empty())
        g_secret = secret;
    if (accessTtlSec > 0)
        g_accessTtl = accessTtlSec;
    if (refreshTtlSec > 0)
        g_refreshTtl = refreshTtlSec;
}

std::string signAccessToken(const TokenPayload& p) {
    auto now = std::chrono::system_clock::now();
    auto builder = jwt::create()
                       .set_type("JWT")
                       .set_subject(std::to_string(p.userId))
                       .set_payload_claim("username", jwt::claim(p.username))
                       .set_payload_claim("dept_id", jwt::claim(std::to_string(p.deptId)))
                       .set_payload_claim("roles", jwt::claim(p.roles.begin(), p.roles.end()))
                       .set_payload_claim("data_scope", jwt::claim(p.dataScope))
                       .set_payload_claim("custom_dept_ids", jwt::claim(p.customDeptIds.begin(),
                                                                        p.customDeptIds.end()))
                       .set_payload_claim("session_id", jwt::claim(p.sessionId))
                       .set_payload_claim("typ", jwt::claim(std::string("access")))
                       .set_issued_at(now)
                       .set_id(p.jti)
                       .set_expires_at(now + std::chrono::seconds(g_accessTtl.load()));
    return builder.sign(jwt::algorithm::hs256{g_secret});
}

std::string signRefreshToken(int64_t userId, const std::string& sessionId) {
    auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_subject(std::to_string(userId))
        .set_payload_claim("session_id", jwt::claim(sessionId))
        .set_payload_claim("typ", jwt::claim(std::string("refresh")))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(g_refreshTtl.load()))
        .sign(jwt::algorithm::hs256{g_secret});
}

std::optional<TokenPayload> verifyAccessToken(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        jwt::verify().allow_algorithm(jwt::algorithm::hs256{g_secret}).verify(decoded);
        if (decoded.get_payload_claim("typ").as_string() != "access")
            return std::nullopt;

        TokenPayload p;
        p.userId = std::stoll(decoded.get_subject());
        p.username = decoded.get_payload_claim("username").as_string();
        p.deptId = std::stoll(decoded.get_payload_claim("dept_id").as_string());
        for (const auto& r : decoded.get_payload_claim("roles").as_array())
            p.roles.push_back(r.as_string());
        p.dataScope = static_cast<int>(decoded.get_payload_claim("data_scope").as_integer());
        for (const auto& d : decoded.get_payload_claim("custom_dept_ids").as_array())
            p.customDeptIds.push_back(d.as_integer());
        p.sessionId = decoded.get_payload_claim("session_id").as_string();
        p.jti = decoded.get_id();
        p.iat = std::chrono::system_clock::to_time_t(decoded.get_issued_at());
        p.exp = std::chrono::system_clock::to_time_t(decoded.get_expires_at());
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<nlohmann::json> verifyRefreshToken(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        jwt::verify().allow_algorithm(jwt::algorithm::hs256{g_secret}).verify(decoded);
        if (decoded.get_payload_claim("typ").as_string() != "refresh")
            return std::nullopt;
        nlohmann::json j;
        j["user_id"] = std::stoll(decoded.get_subject());
        j["session_id"] = decoded.get_payload_claim("session_id").as_string();
        return j;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace hms::JwtUtils
