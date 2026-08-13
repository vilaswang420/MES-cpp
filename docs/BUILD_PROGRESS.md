# HMS 构建与验证进展

> 记录首次真实构建验证（计划任务 s1-s5）的过程与结论。最近更新：2026-08-13。

## 总览

| 任务 | 状态 | 结论 |
|---|---|---|
| s1 本机工具链摸底 | ✅ 完成 | VS2022 Build Tools (MSVC 14.44) + CMake + vcpkg + Node 就绪；VS Professional 实例注册损坏，改用 Build Tools + 环境变量方案 |
| s2 报工并发超报测试 | ✅ 完成 | `tests/` 下并发超报用例，随 s3 编译运行全绿 |
| s3 首次真实编译 hms-backend | ✅ 完成 | 6 轮迭代，203+ 错误 → 0；`hms-backend.exe` 构建成功；ctest 1/1 通过（提交 `901e805`） |
| s4 前端构建验证 | ✅ 完成 | hms-web / hms-dashboard npm install + tsc + vite build 全部通过 |
| s5 中间件联调 + 迁移往返 | ✅ 完成 | 3 容器全 healthy；migrate 全量 up/down/up 往返通过；pg_partman + pg_cron 注册成功 |

## s3 构建修复纪要（6 轮）

### 环境与依赖层

| 问题 | 修复 |
|---|---|
| VS Professional 实例注册损坏，vcpkg/cmake 找不到编译器 | 改装 VS2022 Build Tools；设置 `VS170COMNTOOLS`；向 Build Tools 目录写 `InstallationVersion` 文件 |
| vcpkg 无 bcrypt port | vendored trusch/libbcrypt（MIT/public domain），纯 C 编译 |
| bcrypt 链接失败 `crypt_rn`/`crypt_gensalt_rn` 未解析 | vendored 副本缺 `src/wrapper.c`，从上游补齐并纳入 CMakeLists |
| SimpleAmqpClient 无 CMake 配置 | CMakeLists 手工创建 INTERFACE target（find_library SimpleAmqpClient.7 / rabbitmq.4） |
| 自定义静态链接 triplet | `hms-backend/vcpkg-triplets/x64-windows-hms.cmake` |

### 代码层（Drogon 1.9.13 API 适配）

| 问题 | 修复 |
|---|---|
| `execSqlAsync` 异常回调签名不匹配 | FunctionTraits 只认 `void(const DrogonDbException&)`；全量对齐（含误改的 Redis 回调还原为 `RedisException`） |
| `DrogonDbException` 不继承 `std::exception`，无 `what()` | 38 处改 `e.base().what()`（base() 返回 const std::exception&） |
| AMQP API 与旧文档不符 | vcpkg 版为 `AmqpClient` 命名空间新 API；重写 MqProducer / StopCollectionHandler；无 publisher confirms → 由 mq_outbox 重投兜底；消费端手动 ack |
| `newTransactionCoro()` 返回 `shared_ptr<Transaction>` | `trans.` → `trans->`（WorkOrderService 15 处 + OutboxDispatcher） |
| 协程 handler 注册失败（FunctionTraits 无 `first_param_type`） | 协程 handler 首参必须**按值** `HttpRequestPtr`，`const&` 匹配不到协程特化 |
| enum 类型作 SQL 绑定参数（SqlBinder static_assert） | 显式 `static_cast<int>(...)` |
| `drogon::async_run` 返回 void 不可 await | 自定义 `BlockingAwaiter : CallbackAwaiter<bool>` + detached thread，隔离阻塞 AMQP 发布 |
| lambda 捕获遗漏（C3493） | createUser 行回调补捕获 `onErr` |

### 架构层

- 中间件 AOP 重构：Jwt/Rbac/Audit/Trace 四中间件合并为 `CrossCutting`（preHandling/postHandling 挂点），消除 Drogon middleware 执行时序陷阱；RBAC fail-closed（未注册权限映射的路由一律拒绝）。

## s5 中间件联调纪要

### 环境层

| 问题 | 修复 |
|---|---|
| WSL 未安装，Docker 引擎无法启动 | 在线安装被网络重置 → 离线 MSI（`wsl.2.7.11.0.x64.msi`，curl 断点续传）+ msiexec 静默安装 |
| GitHub 在构建容器内拉源码挂起 | postgres Dockerfile 的 curl/git clone 加超时 + ghfast.top 加速回退 |
| migrate CLI 不认 Windows 反斜杠路径 | `-path` 传正斜杠路径（file:// URL 解析） |
| 往返脚本在 PS5.1 下解析报错 | UTF-8 无 BOM 被 GBK 误读 → 补 BOM；`down` 全量需管道传 y 确认 |
| 数据卷首次初始化早于定制镜像，initdb 未执行 | 手工补 `CREATE SCHEMA partman` + `CREATE EXTENSION pg_partman/pg_cron` |

### 迁移 SQL 层

| 问题 | 修复 |
|---|---|
| DO 块内嵌 `$$SELECT ...$$` 同名 dollar-quote 提前截断语句 | 内层改 `$cron$...$cron$`（001/004） |
| 列名 `offset` 为 PG 保留字 | 改名 `addr_offset`（iot_points） |
| pg_partman 5.x 分区后缀固定 `YYYYMMDD`，与手工预建分区命名不一致导致 overlap 冲突 | 预建分区命名对齐 `_pYYYYMMDD`；`create_parent` 增加 `p_start_partition` 复用已建分区 |

### 验证结论

- hms-postgres / hms-redis / hms-rabbitmq 均 `healthy`。
- 开发库 `hms`：6 个迁移全量 up 成功；`partman.part_config` 2 行（sys_audit_logs 1 mon/24 months、iot_raw_data 1 day/90 days）；`cron.job` 2 个维护作业。
- 往返测试（独立库 hms_roundtrip）：up 全量 → 跨分区插入（当月/次月）→ down 全量 → 再 up 全量，全部通过。

## 验证结论

- `cmake --build ... --config Release`：0 错误，`hms-backend.exe` 1.29 MB。
- `ctest -C Release`：`hms_unit_tests` 1/1 通过（含报工并发超报防护用例）。
- 提交：`5016467`（骨架）→ `5ba5626`（首次构建修复+测试）→ `901e805`（全量编译通过+单测全绿）。

## 遗留事项

1. **publisher confirms**：vcpkg 版 SimpleAmqpClient 无 confirm API，配置项保留，待库升级后启用；当前由 mq_outbox 重投保证最终一致。
2. 集成测试（真实 DB 的 API 级测试）待后续补充。
3. 已有环境补扩展的临时操作已脚本化（`.hms-stage/s5-ext.ps1`）；全新环境由 initdb 自动完成，无需干预。

## M1 出口与 M2 进展纪要（2026-08-13）

### M1 出口（全部通过）

- E2E `m1_flow.ps1`、并发超报 `concurrent_report.ps1`、权限映射门禁（76 路由）、单测 26/26 全绿。
- k6 第 7 轮达标：P95=283.98ms、http_req_failed=0.002%、api_error=0.001%，3504 rps / 210 万迭代。
- 关键修复：`genWorkOrderNo` 改全局序列（随机数碰撞）；k6 脚本 fail-fast + 30s ramp。

### M2 联调关键修复

| 问题 | 修复 |
|---|---|
| `WS_PATH_ADD` 宏不带尾分号 → C2146 | 每行手动补 `;` |
| hiredis 订阅连接 `redisSetTimeout` 后永远读不到消息（err=6 忙旋） | 订阅连接改无超时阻塞读 |
| docker exec 传 JSON 引号被吃 | payload/sh 脚本写文件 → docker cp → 容器内执行 |
| WS 冒烟 ReceiveAsync 无法取消 | Task.WhenAny + Task.Delay 超时模式 |
