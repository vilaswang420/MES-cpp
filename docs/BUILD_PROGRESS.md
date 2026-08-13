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

### m2-integ 集成域（计划任务 23）

交付：`CircuitBreaker.hh`（CLOSED/OPEN/HALF_OPEN，连续 5 失败熔断，30s 冷却半开探测，成功重置计数）+ `IntegrationService`（drogon HttpClient 协程外呼 + retry_count 短退避重试 + integ_sync_logs 全量记录 + 失败日志人工重发）+ 完工回报 Saga（T1 本地完工 → T2 ERP 回报 → T3 WMS 入库，任一步失败逆序补偿）+ 7 接口 + Python 桩（故障注入）。迁移 009 播种权限与 integ_api_configs（dev 指向 9095 桩）。冒烟 20/20，单测 30/30，权限映射门禁 83 路由通过。

修错纪要：`auto path = "/wms/..."` 推导 const char* 致 `"POST " + path` 指针加法 C2110（改显式 std::string）；Windows max 宏污染 `(std::max)`；`sleepCoro` 需 (loop, 秒) 两参；含 mutex 类不能进 map 初始化列表（unique_ptr 懒创建）；PS 5.1 异常响应 body 在 `$_.ErrorDetails.Message`；工单动作接口是 PUT。

### m2-iot 入库链路（计划任务 18/19）

验证：模拟器 1 万条 5s 内全量入库（多行 VALUES 批量 500/100ms）；Redis `device:latest:{id}` 更新 + device.status 广播；毒消息 x-retry-count 3 次有界重试（retry.queue TTL 10s）后进 iot.dlq（约 30s）。冒烟 6/6。

关键事故：`iot.exchange→iot.retry.queue (retry.data)` 绑定丢失导致重试链路断裂（消息无绑定被静默丢弃，队列全空 DLQ 永不增）；新增 `scripts/apply_mq_topology.py` 幂等补建 + `check_mq_topology.py` 复验双保险；management API 建绑定用 POST（PUT 报 405）。

### M2 出口验证（全部通过）

| 出口标准 | 实测 |
|---|---|
| 大屏延迟 < 2s | `scripts/ws_load.py --mode latency` 100/100 样本：P50=186ms / P95=309ms / max=309ms |
| 1000 WS 连接 | `ws_load.py --mode load` 1000/1000 连接成功并存活全程 |
| 复合压测 REST P95 劣化 < 30% | `perf/k6/m2_composite.js` 与 1000 WS + 持续入库（均值 3.7k msg/s）10min 同跑：P95=277.67ms（基线 284ms，劣化 -2.2%）、failed=0.01%、api_err=0.01%、3099 rps |
| 告警→WS→看板弹窗 | 浏览器实证：新建 ALERT-DEMO 设备+传感器（alarm_high=50）→ 发 99.5/105 越限消息 → iot_alerts 落库 → Redis PUBLISH → WS → 大屏 alert 面板实时弹窗（截图 docs/screenshots/dashboard_alert_demo.png） |

容量校准（本机单机）：发布端 pika 峰值 12.7k msg/s（burst 无 confirm，正常 confirm 模式仅 ~104/s），计划 20k msg/s 不可达；**高负载+表 75 万行后持续消费速率实测 ≈1.3k msg/s**（空载探测 ≥5k/s），过载积压 144 万条 purge（模拟器数据）；prefetch 50→200（rabbitmq.json）。

修错纪要：`ws_load.py` 初版按 `env.get("type")` 过滤永远不命中（信封契约无 type 字段，改按 channel）；hms-dashboard 直连后端登录被 CORS 拦截 → 改 vite proxy（/api + /ws）同源接入，useChannel 补自动登录取 token（后端 /ws 严格校验 query token）；传感器阈值快照缓存 60s，新建传感器后需等刷新才能触发告警。

## M3 高可用与容量（任务 24-27，2026-08-13）

### 任务 24 生产 compose

`docker-compose.prod.yml` 从 replicas 占位改为显式拓扑：Redis Cluster redis-1..6 独立卷 + `redis-cluster-init` 幂等建群容器（`--cluster-replicas 1`）；RMQ rabbitmq-1/2/3 显式服务（共享 `HMS_MQ_COOKIE` + `rabbitmq-cluster.conf` DNS 对等发现 + 独立卷）；PG 主从（primary 加 `wal_level=replica`/`max_wal_senders`，replica 由 `init_replica.sh` pg_basebackup -R -C 预填充）；Nginx TLS+WSS 配 `scripts/gen_selfsigned_cert.ps1`（实测生成 hms.crt/hms.key，含 SAN）。`docker compose config` 校验通过。backend 镜像由 CI 产出（当前仓库仅本机 exe，为已知遗留）。

### 任务 25 PgBouncer transaction + 读写分离

会话无关审查：全后端代码无 LISTEN/NOTIFY/临时表/游标/会话级 SET；advisory lock 仅 `pg_try_advisory_xact_lock`（事务结束即释放，transaction 模式安全）；Drogon 参数化语句为单往返扩展协议，不在服务端保留命名 prepared。灰度实证：edoburu/pgbouncer（`-p 6432:5432` + `AUTH_TYPE=scram-sha-256`，compose_default 网络直连 hms-postgres）起池，实例 B（drogon_config.b.json，port 6432）重启后登录/工单列表/报工事务全通；`pg_stat_activity` 对照：A 直连 64 条 vs B 经池仅 2 条服务端连接。切换依据实证：双实例 2×64 曾报 too many clients（PG 默认 100），已调 max_connections=300。只读副本 DSN `HMS_PG_RO_DSN` 已在 prod compose 声明。

### 任务 26 无状态扩容实测（双实例 8088/8089）

代码改造（WsBroadcastManager）：① `publish()` 一律经 Redis `PUBLISH ws:broadcast:{channel}` 扇出（发完整信封），订阅端收到带 version/channel/payload 的信封直接入合并窗口，裸载荷走 `publishLocal` 本实例包装（防双层信封）；② realtime 生产者改 leader 选举：Redis 租约 `ws:realtime:leader`（PX 3000，1Hz GET→XX 续约/NX 抢占），全集群仅一实例查库生产。

实测：跨实例广播——同连两实例订阅 production.realtime+alert，两实例均收齐（`gate_cross_instance_broadcast=true`）；outbox 恰好一次——双实例投递器并发运行下，造 plan_qty=10 工单在实例 B 报满触发 stop_collection，临时绑定队列计数收到恰好 1 条（`gate_outbox_exactly_once=true`，advisory lock 互斥 + SKIP LOCKED）。注：工单列表字段是 `data.list`；只有报满完工才写 outbox，pause 不写。

### 任务 27 容量总验收（校准版）

结论：本机复合门禁通过（M2 出口 10min 数据：P95=277.67ms / failed=0.01% / api_err=0.01% / 3099 rps）；计划原目标（20k msg/s + 5k QPS + 2h）本机不可达：发布峰值 12.7k burst、高负载持续消费 ≈1.3k/s。GA 门槛：标准环境按 m2_composite.js 加长至 2h 复跑三指标 + DLQ 增量≈0 + 分区巡检。

## M3 可观测性与发布演练（任务 28-29，2026-08-13）

### 任务 28 可观测性 + 看板降级

后端内置 Prometheus 端点：`src/utils/Metrics.hh`（counter/gauge/histogram 注册表 + render，9 桶 10~2000ms）+ `src/metrics/MetricsCollector`（outbox 待投递/WS 订阅数 5s，分区剩余天数 60s，MQ lag 独立线程经 `DeclareQueueWithCounts` passive 查询）+ `MetricsController`（GET /metrics，公开白名单）；埋点：CrossCutting preSendingAdvice 记 HTTP 计数+直方图，WsBroadcastManager 记 published/delivered，OutboxDispatcher 记 dispatched/failed。实测 /metrics 全部指标有值（hms_partition_days_left=0、四队列 lag、broadcast published 持续增长）。`deploy/prometheus/prometheus.yml`（15s 抓取）+ `alerts.yml`（6 条规则：PartitionDaysLeftLow<7d critical / MqDataQueueBacklog>10万 / DlqGrowing / OutboxPendingHigh>100 / HttpP95High>369ms / Http5xxRate>0.5%），prometheus 服务声明进 prod compose（hms_prometheus 卷）。

看板降级策略：useChannel 连续重连失败≥3 置 `degraded` → App.vue watch 切 REST 10s 轮询（在制工单首条组装 realtime 同构 payload 含 `degraded_source:true` + 告警历史 20 条），WS 恢复自动复位。浏览器实证（vite `HMS_PROXY_WS` 把 /ws 代理指死端口 18999，REST 正常）：降级横幅「降级模式：实时链路不可用，展示历史数据 (10s 轮询)」+ 历史工单 WO2026081300035 + 告警 20 条，截图 docs/screenshots/degrade_step1_ws_down.png / degrade_step2_final.png。

### 任务 29 发布演练（五阶段五门禁全过）

`scripts/release_drill.ps1` 实测结果：① expand 启新版 C(8090, drogon_config.c.json) healthz=ok；② 蓝绿 nginx（`deploy/nginx/nginx.drill.conf`，split_clients $request_id 10% green + X-HMS-Slot 响应头，8443 TLS）200 请求实测 green%=10/11.5/13.5 → `gate_canary_10pct=true`；③ 回滚 = 宿主机侧删 green 行（.NET API 无 BOM UTF-8 读写，:ro 挂载容器内直接可见）+ reload，100 请求 green=0 → `gate_rollback_zero_traffic=true`；④ contract kill C + 撤演练 nginx → `gate_contract_c_down=true`；⑤ kill B → leader 租约保持且 A 持续发布（published +120/6s）→ `gate_leader_takeover=true`；B 重启 0.5s healthz=ok → `gate_self_heal_30s=true`。

修错纪要：nginx split_clients 不接受 0%（invalid percent value）→ 回滚改删行；bind-mount 文件容器内 sed -i Resource busy + PS5.1 向 docker exec 传 sed 引号不稳 → 宿主机侧改文件；SimpleAmqpClient DeclareQueue 返回队列名非消息数 → DeclareQueueWithCounts；分区名 substring from 17 截掉 "20" → from 15（iot_raw_data_p 前缀 14 字符）。
