#ifndef __BCRYPT__
#define __BCRYPT__

// 跨平台统一实现: 均使用仓库内 vendored bcrypt.h (声明 bcrypt_gensalt/hashpw/checkpw)
// + 下方 BCrypt 类 (底层由 mes_bcrypt C 库 bcrypt.c 实现, 该 C 库含 _WIN32 分支, 故 Windows 也可用)。
// 原 _WIN32 分支误引 windows CNG 的 winbcrypt.h, 既导致 BCRYPT_KEY_HANDLE 未声明,
// 又不定义 BCrypt 类, 在 Windows 下根本无法编译。此处统一走与 Linux 一致的路径。
#include "mes_bcrypt.h"
#include <string>
#include <stdexcept>

class BCrypt {
public:
    static std::string generateHash(const std::string & password, int workload = 12){
        char salt[BCRYPT_HASHSIZE];
        char hash[BCRYPT_HASHSIZE];
        int ret;
        ret = bcrypt_gensalt(workload, salt);
        if(ret != 0)throw std::runtime_error{"bcrypt: can not generate salt"};
        ret = bcrypt_hashpw(password.c_str(), salt, hash);
        if(ret != 0)throw std::runtime_error{"bcrypt: can not generate hash"};
        return std::string{hash};
    }

    static bool validatePassword(const std::string & password, const std::string & hash){
        return (bcrypt_checkpw(password.c_str(), hash.c_str()) == 0);
    }
};

#endif // __BCRYPT__
