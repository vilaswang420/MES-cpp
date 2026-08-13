# HMS 项目交接文档

> 面向新会话 / 新接手者的完整交接。阅读本文后再开始工作。最近更新：2026-08-13（M1 出口全部门禁通过含 k6 第 7 轮；M2 已完成 MQ/设备/质量/WS+大屏，待办 IoT 入库、ERP/WMS 集成、M2 出口）。

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
| 23 ERP/WMS 集成 | ⏳ 待办：迁移 006 表已就绪，缺 IntegrationService/CircuitBreaker/Saga 代码 |
| 18 IoT 模拟器链路 | ⏳ 待办：`scripts/iot_simulator.py` 1 万条 + 毒消息 DLQ 验证 |

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

- ✅ hms-postgres / hms-redis / hms-rabbitmq 三容器运行中且 healthy（docker compose 起的，重启机器后需重新 `up -d`，数据卷持久）。
- ✅ 开发库 `hms` 已迁移至最新版本；`partman.part_config` 2 行、`cron.job` 2 个维护作业。
- ✅ `hms-backend/build/Release/hms-backend.exe` 联调全通：启动命令
  `Start-Process .\build\Release\hms-backend.exe -WorkingDirectory hms-backend`（工作目录必须是 hms-backend，配置相对路径 config/*.json）。
- ✅ Redis 权限缓存（`perm:user:{id}`）、JWT 黑名单、审计刷盘、outbox 投递器均实测验证。
- ⚠️ hms-web / hms-dashboard 的 `node_modules` 与 `dist` 已被清理，需要时重新 `npm install && npm run build`（s4 已验证可构建）。

## 六、遗留事项

1. **publisher confirms**：vcpkg 版 SimpleAmqpClient 无 confirm API，配置项已预留；当前由 `mq_outbox` 表重投保证最终一致，待库升级后启用。
2. Redis Cluster 模式为 M3 阶段事项，dev 环境显式禁用了 cluster。
3. **下一步 = M2**：MQ 拓扑声明验证（deploy/mq/topology.json）→ IoT 模拟器+采集入库 → 设备/传感器/告警 REST → WS 广播+大屏 → 质量域 7 接口 → ERP/WMS 集成（熔断+Saga）。
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
| `scripts/check_mq_topology.py` | MQ 拓扑声明与 broker 一致性门禁 |
