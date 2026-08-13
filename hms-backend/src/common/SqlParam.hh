#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/orm/SqlBinder.h>
#include <drogon/utils/coroutine.h>

#include <coroutine>
#include <cstdio>
#include <memory>
#include <string>

// SQL 绑定参数工具。
//
// 背景: Drogon 对数值绑定参数默认按 C++ 类型字节宽度以二进制发送
// (int=4B / int64_t=8B / bool=1B), 一旦与目标列实际宽度不符
// (本库大量 status/priority/shift 列为 smallint=2B), PG 即报
// "incorrect binary data format in bind parameter N"; 参与 NULLIF/COALESCE
// 等表达式时 PG 还会按字面量推断参数类型, 进一步放大该问题。
//
// 解法: 数值/布尔一律以文本格式 (format=0) 绑定, 交由 PG 解析,
// 对任何数值列宽度均安全。使用方式:
//   db->execSqlAsync("... $1 ...", cb, ecb, SqlArg(userId), SqlArg(status));
// 字符串与 std::string 无需包装 (drogon 本来就按文本发送)。
namespace hms {

inline drogon::orm::RawParameter SqlArg(std::string s) {
    auto obj = std::make_shared<std::string>(std::move(s));
    return drogon::orm::RawParameter{obj, obj->data(), static_cast<int>(obj->size()), 0};
}

inline drogon::orm::RawParameter SqlArg(int64_t v) {
    return SqlArg(std::to_string(v));
}

inline drogon::orm::RawParameter SqlArg(int v) {
    return SqlArg(std::to_string(v));
}

inline drogon::orm::RawParameter SqlArg(bool v) {
    return SqlArg(std::string(v ? "t" : "f"));
}

inline drogon::orm::RawParameter SqlArg(double v) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%.10g", v);
    return SqlArg(std::string(buf));
}

// SQL NULL (参数类型由 PG 按目标列推断)
inline drogon::orm::RawParameter SqlArgNull() {
    auto obj = std::make_shared<std::string>();
    return drogon::orm::RawParameter{obj, nullptr, 0, 0};
}

// 等待事务 COMMIT 真正落库。
// Drogon 的 Transaction 在析构时才异步发出 COMMIT, 若协程在提交前返回响应,
// 调用方紧接着读取会看到旧数据 (写后读不一致)。用法:
//   bool ok = co_await commitAwait(std::move(trans));
// 仅可在事务成功路径调用: 语句失败会抛异常使协程直接退出, 不会走到此处,
// 因此析构必发出 COMMIT, commitCallback 必触发。
inline auto commitAwait(std::shared_ptr<drogon::orm::Transaction> trans) {
    struct Awaiter {
        std::shared_ptr<drogon::orm::Transaction> trans;
        bool ok = false;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            auto* loop = drogon::app().getLoop();
            trans->setCommitCallback([this, h, loop](bool committed) mutable {
                ok = committed;
                // 回到应用主循环唤醒, 避免跨 IO 线程 resume 引发线程断言
                loop->queueInLoop([h] { h.resume(); });
            });
            trans.reset(); // 引用归零 -> 析构 -> 发出 COMMIT, 完成后回调唤醒
        }
        bool await_resume() noexcept { return ok; }
    };
    return Awaiter{std::move(trans)};
}

} // namespace hms
