# HMS 项目交接文档

> 面向新会话 / 新接手者的完整交接。阅读本文后再开始工作。最近更新：2026-08-13（M1 出口全部门禁通过含 k6 第 7 轮；M2 全部完成含出口验证；M3 任务 24-27 完成：生产 compose 定稿、PgBouncer transaction 灰度实证、双实例无状态扩容三项实测、容量校准版验收结论，剩任务 28-29）。

## 一、项目概况

HMS 是制造执行系统（MES），monorepo 结构：

| 目录 | 说明 |
|---|---|
| `hms-backend` | C++ Drogon 1.9.13 后端（REST + WebSocket + MQ outbox） |
| `hms-web` | React 18 + TS + Vite 主前端 |
| `hms-dashboard` | Vue 3 + TS + Vite 车间看板 |
| `hms-iot` | IoT 数据采集模块 |
| `deploy` | docker-compose（dev/prod）、定制 postgres 镜像（pg_partman + pg_cron）、MQ topology、nginx、pgbouncer |
| `contracts` | OpenAPI 契约 |
| `scripts` | 工具脚本（含迁移往返测试） |
| `tests` / `perf` / `spike` | 测试 / k6 压测 / 技术验证 |
| `docs` | 架构设计与进展纪要（见 `docs/BUILD_PROGRESS.md`） |
| `Justfile` | 统一构建入口（`just dev-up` / `just migrate-up` 等） |

中间件：PostgreSQL 16（分区 + 定时维护）、Redis 7（dev 单实例）、RabbitMQ 3.13-management。

## 二、总体进展（s1-s5 全部完成 ✅）

| 阶段 | 结论 |
|---|---|
| s1 工具链摸底 | VS2022 Build Tools (MSVC 14.44) + CMake + vcpkg + Node 就绪 |
| s2 并发超报测试 | 报工并发超报防护用例，随 s3 全绿 |
| s3 后端首次真实编译 | 6 轮迭代 203+ 错误 → 0；`hms-backend.exe` 构建成功；ctest 1/1 通过 |
| s4 前端构建验证 | hms-web / hms-dashboard npm install + tsc + vite build 通过 |
| s5 中间件联调 | 3 容器全 healthy；migrate 全量 up/down/up 往返通过；partman + cron 注册成功 |

提交链（本仓库）：`5016467` 骨架 → `5ba5626` 构建修复+测试 → `901e805` 编译全绿 → `6142706` 进展文档 → `bdeb336` s5 修复。

详细修错纪要见 `docs/BUILD_PROGRESS.md`。

### M1 出口验证进展（2026-08-13）

| 门禁 | 结论 |
|---|---|
| E2E `tests/e2e/m1_flow.ps1` | ✅ 全流程通过：登录→建主数据→建单 0→1→2→3→报工×2→自动完工(status=5)→outbox 已投递→operator 403 反例→data_scope 404 反例→审计分页 |
| 并发超报 `tests/e2e/concurrent_report.ps1` | ✅ 8 路并发报最后一件：恰好 1 成功 + 7 个 409，completed_qty=1，停采 outbox 恰好 1 条 |
| 权限映射门禁 `scripts/check_perm_mapping.py` | ✅ 76 条路由全注册，与 002_seed 一致 |
| 单测 | ✅ 26/26（状态机/data_scope/JWT） |
| k6 基线 `perf/k6/m1_baseline.js` | ✅ 第 7 轮三门禁全过：P95=283.98ms（<300）、http_req_failed=0.002%（<0.5%）、api_error=0.001%（<0.5%）；3504 rps / 210 万迭代；结果在 `perf/k6/m1_baseline_result.txt` |

k6 第 5-7 轮结论纪要：
- 第 5 轮 2.57% 错误实为 k6 setup 与后端重启窗口重叠 → woIds 含 null → `/null/report` 数据污染（非后端容量问题）；脚本已加 fail-fast（setup 失败 throw + VU 段 abort）。
- 第 6 轮无 ramp 时 P95=303ms 差 3ms 未过；第 7 轮加 30s 线性 ramp（总时长 10min / 峰值 500 VU 不变）后达标。
- 顺带根治 `genWorkOrderNo` 随机三位数碰撞（库中同日 200+ 工单时碰撞率≈20% → 500）：改全局序列 `prod_work_order_no_seq`，迁移 003 up/down 同步。

M1 期间根治的三类 drogon 陷阱（新增代码必须遵守，详见踩坑清单 18-21）：
- 数值绑定参数一律用 `hms::SqlArg()` 文本化（`src/common/SqlParam.hh`），禁止裸传数值；
- 协程事务响应前必须 `co_await commitAwait(std::move(trans))`；回调式事务把 onOk 移入 `setCommitCallback`；
- 定时 fire-and-forget 协程必须用 eager 的 `drogon::AsyncTask` 包装启动（`Task` 是惰性的）。

### M2 进展（2026-08-13）

| 任务 | 结论 |
|---|---|
| 17 MQ 拓扑 | ✅ `deploy/mq/topology.json` 声明与 broker 实际一致（`scripts/check_mq_topology.py`） |
| 19 入库/告警消费 | ✅ `DataIngestHandler`（批量 COPY 入库）+ `AlertHandler`（告警落库+Redis 发布）已装配于 main.cc |
| 20 设备域 REST | ✅ 设备/传感器/告警/采集任务 18 接口，冒烟 35/35（`tests/e2e/m2_qc_iot_smoke.ps1`） |
| 21 WS 链路+大屏 | ✅ `WsBroadcastManager`（Redis Pub/Sub 订阅 + 200ms 合并窗口 + 1Hz realtime 生产者），WS 冒烟 12/12（`tests/e2e/m2_ws_smoke.ps1`） |
| 22 质量域 | ✅ 检验标准/检验记录/缺陷处置 7 接口，冒烟 35/35 |
| 23 ERP/WMS 集成 | ✅ 熔断器（成功重置计数）+ IntegrationService（外呼/重试/日志/Saga 补偿）+ 7 接口 + 桩 `scripts/erp_wms_stub.py`，冒烟 20/20（`tests/e2e/m2_integ_smoke.ps1`） |
| 18 IoT 模拟器链路 | ✅ 模拟器 1 万条 5s 内全量入库（批量 500/100ms）+ Redis device:latest + 毒消息 3 次有界重试进 DLQ，冒烟 6/6（`tests/e2e/m2_iot_smoke.ps1`） |

### M2 出口验证（2026-08-13，全部通过）

| 出口标准 | 结论 |
|---|---|
| 1 万条全量入库 / 毒消息进 DLQ | ✅ 已含在 m2_iot_smoke 6/6 |
| 大屏延迟 < 2s | ✅ `scripts/ws_load.py --mode latency`：100/100 样本，P50=186ms / P95=309ms / max=309ms |
| 1000 WS 连接 | ✅ `ws_load.py --mode load`：1000/1000 连接成功并存活全程 |
| 复合压测 REST P95 劣化 < 30% | ✅ `perf/k6/m2_composite.js` 与 1000 WS + 持续入库同跑 10min：**P95=277.67ms（较基线 284ms 劣化 -2.2%）**、failed=0.01%、api_err=0.01%、3099 rps |
| 告警→WS→看板弹窗全链路 | ✅ 浏览器实证：传感器越限 → AlertHandler 落库 → Redis → WS → 大屏 alert 面板实时弹窗（截图 docs/screenshots/dashboard_alert_demo.png） |

容量校准依据（本机单机，2026-08-13）：发布端 pika 峰值 12.7k msg/s（burst 无 confirm），计划 20k msg/s 目标本机不可达；复合期间按可持续均值 ~3.7k msg/s 灌入；**高负载下实测持续消费速率 ≈ 1.3k msg/s**（低于空载探测 ≥5k/s，表增长+复合负载所致），过载积压 144 万条后 purge（模拟器数据）。消费 lag 在发布≤消费容量时实测 < 2s（延迟验证）。

### M3 进展（2026-08-13，任务 24-27 完成）

| 任务 | 结论 |
|---|---|
| 24 生产 compose | ✅ `deploy/compose/docker-compose.prod.yml` 定稿（`docker compose config` 校验通过）：Redis Cluster 3主3从显式节点+独立卷+`redis-cluster-init` 幂等初始化容器；RMQ 3 节点显式服务（共享 ERLANG_COOKIE + DNS 对等发现 `rabbitmq-cluster.conf` + 独立卷）；PG 主从（`wal_level=replica` + 复制槽，`deploy/postgres/init_replica.sh` pg_basebackup 初始化）；Nginx TLS+WSS（`scripts/gen_selfsigned_cert.ps1` 已实测生成证书）；backend replicas=2 |
| 25 PgBouncer transaction + 读写分离 | ✅ 会话无关审查通过：全代码无 LISTEN/NOTIFY/临时表/游标/会话级 SET，advisory lock 仅用事务级 `pg_try_advisory_xact_lock`，Drogon 参数化语句单往返不驻留服务端；**灰度实证**：实例 B 经 PgBouncer（edoburu 镜像，transaction 模式）跑登录/列表/报工事务全通，PG 侧仅 2 条服务端连接复用 64 客户端（对照实例 A 直连 64 条）；切换依据：双实例 2×64 连接曾超 PG 默认 max_connections=100（已调 300）；只读副本 DSN 已声明 `HMS_PG_RO_DSN` |
| 26 无状态扩容验证 | ✅ 三项实测全过：① WS 跨实例广播——双实例（8088/8089）客户端均收到 realtime+alert（`gate_cross_instance_broadcast=true`）；② realtime 生产者 leader 选举（Redis 租约 `ws:realtime:leader`，全集群单实例生产）；③ outbox 恰好一次——双实例投递器并发运行下，临时队列计数收到 stop_collection 恰好 1 条（`gate_outbox_exactly_once=true`，advisory lock 互斥）；JWT 黑名单/权限缓存本就全走 Redis；IoT 副本数公式 ceil(设备数/5000) 已在设计文档 |
| 27 容量总验收（校准版） | ✅ 结论：本机复合门禁通过（M2 出口 10min：P95=277.67ms / failed=0.01% / api_err=0.01%）；计划原目标 20k msg/s + 5k QPS + 2 小时本机不可达（发布峰值 12.7k burst、持续消费 ≈1.3k/s，见容量校准依据）——**GA 前须在标准环境按 `perf/k6/m2_composite.js` 加长至 2h 复跑**，本机校准版作为门禁基线 |

## 三、环境与工具链（重要，坑多）

### 3.1 本机工具位置

| 工具 | 位置 / 说明 |
|---|---|
| docker.exe | `C:\Users\vilas\AppData\Local\Programs\DockerDesktop\resources\bin\docker.exe`（**用户级安装，不在 PATH**） |
| just / migrate / k6 | `E:\Work\Development\Tools\bin\`（不在 PATH，Justfile 内直接调用会失败，需先补 PATH 或用绝对路径） |
| VS2022 Build Tools | 非标准实例注册，靠环境变量方案（见下） |
| vcpkg triplet | `hms-backend/vcpkg-triplets/x64-windows-hms.cmake`（自定义静态链接） |
| WSL | 2.7.11，离线 MSI 安装（在线安装会被网络重置） |

### 3.2 VS/vcpkg 特殊配置（勿动）

- vcpkg 识别 Build Tools 依赖 `VS170COMNTOOLS` 环境变量。
- Build Tools 目录下手工写入过 `InstallationVersion` 文件（绕过实例注册检测），删除会导致 vcpkg/cmake 找不到编译器。
- VS Professional 实例注册是损坏的（`%LOCALAPPDATA%` 下坏残留），**不要**尝试 repair VS Professional，会反复失败；一切以 Build Tools 为准。

### 3.3 网络环境

- 本机出网慢（约 100-250 KB/s），GitHub objects CDN 直连经常被重置。
- 大文件下载用 `curl -C -` 断点续传；GitHub 源码拉取用 `ghfast.top` 代理回退（postgres Dockerfile 已内置）。
- Docker Hub 拉取慢但未被封，耐心等待即可。

## 四、常用命令

```powershell
# 补 PATH（每个新终端都需要，docker/just/migrate 都不在 PATH）
$env:PATH = 'C:\Users\vilas\AppData\Local\Programs\DockerDesktop\resources\bin;E:\Work\Development\Tools\bin;' + $env:PATH

# 中间件
docker compose -f deploy/compose/docker-compose.dev.yml up -d --build
docker ps --format "table {{.Names}}\t{{.Status}}"

# 迁移（-path 必须正斜杠，Windows 反斜杠会报 invalid port）
$mig = ((Get-Location).Path -replace '\\','/') + '/hms-backend/migrations'
migrate -path $mig -database 'postgres://hms:hms_dev_pwd@localhost:5432/hms?sslmode=disable' up

# 迁移往返测试（独立库 hms_roundtrip）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-migrate-roundtrip.ps1

# 后端构建（s3 验证过的流程）
# 编译前先杀残留进程: Stop-Process -Name cl,MSBuild,ninja,mspdbsrv,link -Force -ErrorAction SilentlyContinue
cmake --build hms-backend/build --config Release
ctest --test-dir hms-backend/build -C Release
```

后端连接串：`postgres://hms:hms_dev_pwd@localhost:5432/hms`；MQ：`hms/hms_dev_pwd@localhost:5672`（管理台 15672）；Redis：`localhost:6379`。

## 五、当前运行时状态（截至 2026-08-13）

- ✅ hms-postgres（max_connections=300）/ hms-redis / hms-rabbitmq 三容器运行中且 healthy；另有 **hms-pgbouncer**（edoburu/pgbouncer，transaction 模式，宿主机 6432 → 容器内 5432，compose_default 网络）。
- ✅ 开发库 `hms` 已迁移至最新版本；`partman.part_config` 2 行、`cron.job` 2 个维护作业。
- ✅ `hms-backend/build/Release/hms-backend.exe` 双实例运行中：实例 A（8088，默认 config，直连 5432）+ 实例 B（8089，`config/drogon_config.b.json`，经 PgBouncer 6432）；启动第二实例：`Start-Process build\Release\hms-backend.exe -ArgumentList 'config/drogon_config.b.json' -WorkingDirectory hms-backend`，双实例一键脚本 `scripts/start_dual_instances.ps1`。
- ✅ Redis 权限缓存（`perm:user:{id}`）、JWT 黑名单、审计刷盘、outbox 投递器均实测验证；realtime 生产者 leader 租约键 `ws:realtime:leader`。
- ⚠️ hms-web 的 `node_modules` 与 `dist` 已被清理，需要时重新 `npm install && npm run build`（s4 已验证可构建）；hms-dashboard 已重新 `npm install`（M2 出口验证时装）。
- ⚠️ `iot_raw_data` 现有 ~75 万行压测数据（分区表，不影响功能）；`iot.dlq` 有 1 条毒消息为验证证据。

## 六、遗留事项

1. **publisher confirms**：vcpkg 版 SimpleAmqpClient 无 confirm API，配置项已预留；当前由 `mq_outbox` 表重投保证最终一致，待库升级后启用。
2. prod compose 为声明+本机校验（config 通过）形态：backend 镜像需 CI 产出（当前仅本机 exe）；Redis/RMQ 集群与 PG 流复制的完整启动需在具备 docker swarm/大内存的环境执行。
3. **下一步 = M3 任务 28-29**：可观测性（Prometheus 指标 + 看板降级策略）+ 发布演练（蓝绿/回滚/kill 实例自愈）。
4. E2E 会在库里留下测试数据（E2E-/CR-/K6- 前缀），不影响断言，如需清理手工删除。

## 七、注意事项（踩坑清单，务必先读）

### PowerShell 环境陷阱

1. **外层 Node fallback shell 会吞 `$` 变量**（`$_`/`$i`/`$d=` 全被吞导致 ParserError）→ 凡是带变量的命令一律写成 `.ps1` 文件用 `powershell -File` 执行，不要写内联 `-Command`。
2. **UTF-8 无 BOM 的中文 ps1 在 PS5.1 下被 GBK 误读** → 脚本含中文必须存为 UTF-8 **with BOM**。
3. `Start-Process -Verb RunAs` 不能与 `-RedirectStandardOutput` 连用 → 提权运行包装脚本，脚本内部用 `*>>` 落盘日志。
4. 终端不支持 `&&` 分隔符，用 `;`。

### 数据库 / 迁移陷阱

5. **pg_partman 5.x 分区后缀固定 `_pYYYYMMDD`**（不随 interval 变化）→ 手工预建分区必须对齐该命名，并在 `create_parent` 传 `p_start_partition` 复用已建分区，否则报 "would overlap partition"。
6. **DO 块内禁止复用外层 `$$` 标签** → 内层字符串用 `$cron$` 等唯一标签，否则 DO 体被提前截断。
7. **列名避开 PG 保留字**（`offset`/`user`/`order` 等），已因此把 `offset` 改名 `addr_offset`。
8. migrate 的 `-path` 在 Windows 上必须正斜杠（按 file:// URL 解析）。
9. `migrate down` 全量需 `down -all` 且交互确认（非交互场景管道传 `y`）。
10. `hms` 库被 pg_cron worker 占用无法 `DROP DATABASE`，重置用 `DROP SCHEMA public CASCADE; CREATE SCHEMA public;`。
11. postgres 数据卷若先于定制镜像初始化，initdb 的扩展创建不会补跑 → 手工 `CREATE SCHEMA IF NOT EXISTS partman; CREATE EXTENSION IF NOT EXISTS pg_partman SCHEMA partman; CREATE EXTENSION IF NOT EXISTS pg_cron;`。

### 构建 / 提交纪律

12. **git add 前排除 node_modules 与嵌套 git 仓库**（曾误提交 22768 个文件，靠 `git rm -r --cached` + amend 修复）。
13. 编译前杀残留 cl/MSBuild/ninja/link 进程，避免文件占用。
14. vendored 的 `third_party/bcrypt` 必须包含 `src/wrapper.c`（`crypt_rn`/`crypt_gensalt_rn` 的实现在这里），且已在 CMakeLists 中。
15. Drogon 协程 handler 首参必须**按值** `HttpRequestPtr`（`const&` 匹配不到 FunctionTraits 协程特化）；enum 作 SQL 绑定参数要 `static_cast<int>`。

### 工作流说明

16. 本仓库（New-HMS）是**唯一权威源码库**。此前会话因工作区限制曾通过 `hm-MES\.hms-stage` 暂存中转编辑；新工作区直接是 New-HMS 后可直接编辑，无需中转。
17. 旧仓库 `hm-MES` 的 `.hms-stage` 只是历史暂存副本，不要把它当作源码改。

### Drogon 运行时陷阱（M1 联调血泪，必读）

18. **drogon `execCommandAsync`（Redis）是 C 变参**：`%s` 必须传 `.c_str()`，传 `std::string` 是 UB（会把指针字节当字符串写入，造成脏缓存/崩溃）。
19. **drogon 数值绑定参数按 C++ 字节宽度二进制发送**（int=4B/int64_t=8B/bool=1B），与 smallint(2B) 列不符即报 `incorrect binary data format in bind parameter N`；参与 NULLIF/COALESCE 表达式还会被 PG 类型推断放大。一律用 `hms::SqlArg()`（format=0 文本绑定）包装数值/布尔。
20. **drogon Transaction 析构才异步 COMMIT**（无显式 commit()），提交前响应会造成写后读不一致：协程式用 `commitAwait(std::move(trans))` 等待；回调式把成功响应移入 `setCommitCallback`，并让最后一个持有 trans 的回调结束时释放引用触发提交。语句失败时 drogon 会自动 rollback，commitCallback 不再触发，注意别双重响应。
21. **drogon `Task` 是惰性协程**（initial_suspend=suspend_always）：`(void)task()` 丢弃返回值永远不会执行；fire-and-forget 必须用 eager 的 `drogon::AsyncTask` 包装（参考 OutboxDispatcher::runTick）。
22. **PS 5.1 Start-Job 环境读不到部分异常响应体**（并发测试 409 读空）：从异常消息正则 `\((\d{3})\)` 提取状态码作主路径；Job 返回纯字符串比 PSCustomObject 更稳。

### M2 联调陷阱（WS/IoT 链路）

23. **drogon `WS_PATH_ADD` 宏展开不带尾分号**（与 `ADD_METHOD_TO` 不同）：多路径注册时每行必须手动补 `;`，否则 MSVC 在下一行报 C2146。
24. **hiredis Windows：订阅连接禁止 `redisSetTimeout`**——设了超时后 `redisGetReply` 会持续立即返回 `REDIS_ERR_TIMEOUT`（err=6 忙旋）而永远读不到已发布的消息；订阅连接用无超时阻塞读，停机靠进程退出兜底（detach 线程）。
25. **docker exec argv 吃引号**：PS → docker exec → redis-cli 传 JSON 时引号被剥掉；解法：payload 与 sh 脚本写本地文件 → `docker cp` 进容器 → `docker exec hms-redis sh /tmp/xxx.sh`（脚本内用 `$(cat ...)`）。
26. **.NET ClientWebSocket 的 `ReceiveAsync` 不响应 CancellationToken**（取消后仍永久阻塞）：超时须用 `Task.WhenAny(recvTask, Task.Delay(...))` 实现。
27. **k6 与后端重启窗口重叠会污染 setup 数据**：k6 脚本必须 fail-fast（setup 登录/建数失败 throw，VU 段校验数据完整性 abort）；高并发压测加短 ramp 避免 t=0 建连风暴抬高尾部延迟。
28. **字符串字面量在前的 `+` 遇 `auto` 推导 `const char*` 即指针加法（C2110）**：`auto wmsPath = "/wms/stock-in"; "POST " + wmsPath` 编译报错；路径类局部变量一律显式 `std::string`。另：Windows `max` 宏污染下 `std::max` 要写 `(std::max)`；`drogon::sleepCoro` 签名是 `(EventLoop*, double秒)` 两参；含 mutex 的类不可拷贝/移动，不能放 map 初始化列表，用 `unique_ptr` 懒创建。
29. **PS 5.1 `Invoke-RestMethod` 抛异常时响应流已被 cmdlet 消费**：`$resp.GetResponseStream()` 读出空串，错误 body 在 `$_.ErrorDetails.Message`；冒烟脚本判熔断/错误消息必须走 ErrorDetails。
30. **工单状态流转动作接口（schedule/release/start/pause/complete/close）是 PUT 不是 POST**（perm_routes 与路由注册一致）；集成冒烟首次挂在这里返回 drogon 空 body 404。
31. **MQ 绑定会丢：拓扑声明与运行态必须双核对**。曾出现 `iot.exchange→iot.retry.queue (retry.data)` 绑定丢失，毒消息重试链路断裂（无绑定消息被静默丢弃，队列全空但 DLQ 永远不增）；排查靠 `rabbitmqctl list_bindings`，恢复用 `scripts/apply_mq_topology.py`（幂等补建）+ `check_mq_topology.py` 复验。注意 management API 建绑定是 **POST** 不是 PUT（405）。
32. **WS 推送信封无 type 字段**（contracts/ws-push.schema.json additionalProperties=false）：客户端过滤推送只能按 `channel` 判，`env.get("type")` 永远不命中。
33. **后端未启 CORS，前端必须同源接入**：hms-dashboard 直连 127.0.0.1:8088 登录被 CORS 拦截；解法与 hms-web 一致——vite dev proxy（/api + /ws 含 ws:true）/生产 Nginx 反代，useChannel 默认用 `location.host` 同源地址。
34. **传感器阈值快照缓存 60s**（DataIngestHandler::sensorSnapshot）：新建传感器后立即发越限消息不会触发告警，需等缓存刷新（最多 60s）再发。
35. **高负载下持续消费速率远低于空载探测值**：空载 3 万条 6s 排空（≥5k/s），复合压测 + 表增长后实测 ≈1.3k/s；容量结论必须标注负载条件，压测编排按可持续速率而非峰值灌入，否则积压无界。

### M3 陷阱（PgBouncer/compose）

36. **edoburu/pgbouncer 镜像容器内监听 5432 不是 6432**：端口映射必须 `-p 6432:5432`；且默认 `auth_type=md5` 与 PG16 的 scram-sha-256 不兼容（报 `wrong password type`），须加 `AUTH_TYPE=scram-sha-256`。
37. **容器内 `host.docker.internal` 连宿主机发布端口不稳**（server login failed / closed connection）：需要访问其他 compose 容器时，用 `--network compose_default` + 容器名直连最可靠。
38. **工单列表响应字段是 `data.list` 不是 `items`**；且只有报工满量自动完工才写 mq_outbox（`OutboxService::kEnqueueSql` 全项目唯一写入点），pause/start 等状态流转不写 outbox，验证投递器需造一个可报满的小量工单。
39. **PS `Join-Path $PSScriptRoot '..\nginx\certs'` 相对路径以脚本目录解析**：scripts 下的脚本引用仓库其它目录必须写全相对层级（如 `..\deploy\nginx\certs`），否则产物落错目录。

## 八、关键文件索引

| 文件 | 用途 |
|---|---|
| `docs/BUILD_PROGRESS.md` | s1-s5 详细修错纪要 |
| `Justfile` | 构建/部署统一入口 |
| `deploy/compose/docker-compose.dev.yml` | 开发中间件编排 |
| `deploy/postgres/Dockerfile` | 定制 PG 镜像（pg_partman 5.1.0 + pg_cron 1.6.4 源码编译） |
| `deploy/postgres/initdb/00_extensions.sql` | 首次初始化建扩展 |
| `scripts/test-migrate-roundtrip.ps1` | 迁移往返测试 |
| `hms-backend/vcpkg.json` | C++ 依赖清单（builtin-baseline 必须是 commit SHA） |
| `tests/e2e/m1_flow.ps1` | M1 出口 E2E 全流程脚本 |
| `tests/e2e/concurrent_report.ps1` | 并发超报防护测试 |
| `hms-backend/src/common/SqlParam.hh` | SqlArg 文本绑定 + commitAwait 提交等待器 |
| `perf/k6/m1_baseline.js` | M1 出口 k6 基线（500 VU 混合 10min，30s ramp + fail-fast） |
| `tests/e2e/m2_qc_iot_smoke.ps1` | M2 设备域+质量域冒烟（35+35 项） |
| `tests/e2e/m2_ws_smoke.ps1` | M2 WS 链路冒烟（12 项，含文件式 RedisPublish） |
| `tests/e2e/m2_integ_smoke.ps1` | M2 ERP/WMS 集成冒烟（20 项：同步/幂等/转工单/Saga/补偿/熔断/重发） |
| `tests/e2e/m2_iot_smoke.ps1` | M2 IoT 入库链路冒烟（6 项：1 万条入库/Redis/毒消息 DLQ） |
| `scripts/apply_mq_topology.py` | MQ 拓扑幂等补建（绑定丢失恢复） |
| `scripts/erp_wms_stub.py` | ERP/WMS 本地桩（9095，故障注入 `/__control`） |
| `hms-backend/src/utils/CircuitBreaker.hh` | 熔断器（header-only，单测 4 用例） |
| `scripts/check_mq_topology.py` | MQ 拓扑声明与 broker 一致性门禁 |
| `scripts/ws_load.py` | M2 出口 WS 延迟/连接数验证（latency/load 双模式，自带门禁输出） |
| `perf/k6/m2_composite.js` | M2 出口复合压测（REST 门禁 P95<369ms = 基线×1.3） |
| `deploy/compose/docker-compose.prod.yml` | M3 生产编排（Redis Cluster 6 节点/RMQ 3 节点/PG 主从/PgBouncer/Nginx TLS） |
| `deploy/compose/rabbitmq-cluster.conf` | RMQ 集群组建（DNS 对等发现） |
| `deploy/postgres/init_replica.sh` | PG 只读副本初始化（pg_basebackup + 复制槽） |
| `scripts/gen_selfsigned_cert.ps1` | Nginx 自签证书生成（已实测） |
| `scripts/start_dual_instances.ps1` | 双实例（8088/8089）一键启动 + healthz 检查 |
| `hms-backend/config/drogon_config.b.json` | 第二实例配置（8089，DB 走 PgBouncer 6432） |
