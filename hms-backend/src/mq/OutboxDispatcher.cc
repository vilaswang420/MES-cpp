#include "mq/OutboxDispatcher.hh"

#include <drogon/drogon.h>
#include <drogon/orm/CoroMapper.h>
#include <drogon/utils/coroutine.h>

#include <atomic>
#include <coroutine>
#include <thread>

#include "common/SqlParam.hh"
#include "mq/MqProducer.hh"

namespace hms::OutboxDispatcher {

namespace {

constexpr int64_t kAdvisoryLockKey = 0x484D5301; // "HMS" + 投递器编号
constexpr int kBatchSize = 50;
constexpr int kMaxRetry = 5;

std::atomic<bool> g_running{false};

// 在独立线程执行阻塞函数 (Drogon 1.9.13 无内置阻塞函数 awaiter),
// 避免同步 AMQP 发布占用 IO 循环
struct BlockingAwaiter : drogon::CallbackAwaiter<bool> {
    explicit BlockingAwaiter(std::function<bool()> fn) : fn_(std::move(fn)) {}

    void await_suspend(std::coroutine_handle<> handle) {
        std::thread([this, handle]() {
            try {
                setValue(fn_());
            } catch (...) {
                setException(std::current_exception());
            }
            handle.resume();
        }).detach();
    }

  private:
    std::function<bool()> fn_;
};

// 单轮扫描: advisory_xact_lock 保证多实例互斥, 事务结束自动释放锁。
// Drogon Transaction 在对象析构时自动提交 (无异常路径), 异常即整体回滚重试。
drogon::Task<> tick() {
    if (g_running.exchange(true))
        co_return; // 上一轮未结束, 跳过
    // 作用域守卫: 任何路径退出(含提前 co_return)都复位运行标志,
    // 否则一次锁竞争失败将永久停掉投递器
    struct Guard {
        ~Guard() { g_running = false; }
    } guard;
    try {
        auto db = drogon::app().getDbClient();
        auto trans = co_await db->newTransactionCoro();

        auto lock = co_await trans->execSqlCoro("SELECT pg_try_advisory_xact_lock($1)",
                                                  SqlArg(kAdvisoryLockKey));
        if (!lock[0][0].as<bool>())
            co_return; // 其他实例正在投递

        // 待投递 + 未超重试上限的失败件
        auto rows =
            co_await trans->execSqlCoro("SELECT id, exchange, routing_key, payload FROM mq_outbox "
                                       "WHERE status = 0 OR (status = 2 AND retry_count < $1) "
                                       "ORDER BY created_at LIMIT $2 FOR UPDATE SKIP LOCKED",
                                       SqlArg(kMaxRetry), SqlArg(kBatchSize));

        for (const auto& row : rows) {
            auto id = row["id"].as<int64_t>();
            auto exchange = row["exchange"].as<std::string>();
            auto routingKey = row["routing_key"].as<std::string>();
            auto payload = row["payload"].as<std::string>();

            // 阻塞式 AMQP 发布放到独立线程执行, 避免占用 IO 循环
            bool ok = co_await BlockingAwaiter([exchange, routingKey, payload] {
                return MqProducer::publishSync(exchange, routingKey, payload);
            });
            if (ok) {
                co_await trans->execSqlCoro(
                    "UPDATE mq_outbox SET status = 1, sent_at = NOW() WHERE id = $1", SqlArg(id));
            } else {
                co_await trans->execSqlCoro(
                    "UPDATE mq_outbox SET status = 2, retry_count = retry_count + 1 "
                    "WHERE id = $1",
                    SqlArg(id));
                LOG_WARN << "outbox dispatch failed, id=" << id;
            }
        }
        // trans 析构 -> 自动提交 (锁随之释放)
    } catch (const std::exception& e) {
        LOG_ERROR << "outbox tick error: " << e.what();
    }
}

// Drogon Task 为惰性协程 (initial_suspend = suspend_always),
// 直接丢弃返回值 (void)tick() 永远不会执行; 须经 eager 的 AsyncTask 启动。
drogon::AsyncTask runTick() {
    try {
        co_await tick();
    } catch (const std::exception& e) {
        LOG_ERROR << "outbox tick error: " << e.what();
    }
}

} // namespace

void start() {
    drogon::app().getLoop()->runEvery(2.0, [] { runTick(); });
    LOG_INFO << "outbox dispatcher started (interval=2s, batch=" << kBatchSize << ")";
}

} // namespace hms::OutboxDispatcher
