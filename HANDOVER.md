# HMS 项目交接文档

> 面向新会话 / 新接手者的完整交接。阅读本文后再开始工作。最近更新：2026-05-13（s1-s5 全部完成）。

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

## 五、当前运行时状态（截至 2026-05-13）

- ✅ hms-postgres / hms-redis / hms-rabbitmq 三容器运行中且 healthy（docker compose 起的，重启机器后需重新 `up -d`，数据卷持久）。
- ✅ 开发库 `hms` 已迁移至最新版本；`partman.part_config` 2 行、`cron.job` 2 个维护作业。
- ✅ `hms-backend/build/Release/hms-backend.exe` 存在（可直接运行联调）。
- ⚠️ hms-web / hms-dashboard 的 `node_modules` 与 `dist` 已被清理，需要时重新 `npm install && npm run build`（s4 已验证可构建）。

## 六、遗留事项

1. **publisher confirms**：vcpkg 版 SimpleAmqpClient 无 confirm API，配置项已预留；当前由 `mq_outbox` 表重投保证最终一致，待库升级后启用。
2. **API 级集成测试**（真实 DB）尚未编写，是下一个自然步骤。
3. **端到端联调**：`hms-backend.exe` 尚未真正跑通全流程（启动 → 登录 → 报工 → MQ 事件），中间件已就绪可随时开始。
4. Redis Cluster 模式为 M3 阶段事项，dev 环境显式禁用了 cluster。

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
