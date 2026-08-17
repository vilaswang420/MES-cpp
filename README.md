# MES 制造执行系统

> 技术栈: C++ Drogon + React 18 / Vue 3 + PostgreSQL 16 + Redis 7 + RabbitMQ 3.13
> 设计事实源: [`docs/MES_Architecture_Design.md`](docs/MES_Architecture_Design.md)

## 项目状态（2026-08-17）

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| S0 | 预研 Spike + ADR 0001 | ✅ 完成 |
| | 工程骨架 + CI | ✅ 完成（healthz / CI 全绿） |
| M1 | 用户权限(A域) + 生产管理(B域) | ✅ 完成（E2E + k6 P95<300ms） |
| M2 | IoT / 质量 / 看板 / 集成 | ✅ 完成（投毒消息进 DLQ + WS<2s） |
| M3 | 高可用与容量验证 | ✅ 完成（2h 全链路压测 GA 达标） |
| P1–P3 | 核心功能完善（正确性/稳定性/安全性） | ✅ 完成（见 [`docs/CORE_PLAN.md`](docs/CORE_PLAN.md)） |
| P4 | 缺失功能补齐（5.1–5.7） | 🟡 评审通过，实施中（见 [`docs/P4_IMPLEMENTATION_PLAN.md`](docs/P4_IMPLEMENTATION_PLAN.md)） |

> 命名说明：本项目原名 HMS（Human Manufacturing System，非标准自造名），已于 **2026-08-17 全量更名为 MES**（含目录、文件名、代码标识符三态大小写）。本地克隆目录 `New-HMS` 为历史路径，逻辑项目名即 MES。

## 仓库结构

```
New-MES/
├── docs/                    # 设计文档 + adr/ 决策记录 + 文档索引(README.md)
├── mes-backend/             # Drogon C++ 后端 (REST + WS + MQ 消费) — 详见 mes-backend/README.md
├── mes-web/                 # React 18 + Vite + AntD 5 管理后台 — 详见 mes-web/README.md
├── mes-dashboard/           # Vue3 + ECharts 5 大屏看板 — 详见 mes-dashboard/README.md
├── mes-iot/                 # 独立 C++ IoT 采集服务 — 详见 mes-iot/README.md
├── contracts/               # MQ/WS/错误响应 JSON Schema 单一事实源
├── deploy/                  # compose / 定制 PG 镜像 / MQ 拓扑 / nginx / pgbouncer
├── perf/k6/                 # 压测脚本 (阶段出口硬门禁)
├── scripts/                 # migrate 往返测试 / 权限映射检查 / IoT 模拟器
├── spike/                   # 一次性 POC (不进主干)
└── tests/                   # E2E 脚本
```

## 快速开始 (M0 DoD)

前置依赖: Docker、[just](https://github.com/casey/just)、[golang-migrate](https://github.com/golang-migrate/migrate)、Node 18+、Python 3.10+、vcpkg（后端/IoT 构建）。

```bash
just dev-up        # compose up + migrate up
# 启动后端 (需 vcpkg + CMake, 见 mes-backend/README.md)
cmake -S mes-backend -B mes-backend/build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build mes-backend/build -j
./mes-backend/build/mes-backend mes-backend/config/drogon_config.json
# 验证四服务
curl http://localhost:8088/healthz
```

默认超管: `admin / password` (仅开发环境, 见 `mes-backend/migrations/002_seed.up.sql`)。

## 常用命令（just 入口）

| 命令 | 说明 |
|------|------|
| `just dev-up` | 起中间件 + 迁移（M0 DoD） |
| `just migrate-up` / `migrate-down` | 数据库迁移 up / 回退 1 步 |
| `just migrate-roundtrip` | 迁移往返测试（CI 同款，含跨分区插入） |
| `just check-perm-map` | fail-closed 权限映射完整性检查（CI 门禁） |
| `just build-backend` / `test-backend` | 后端 CMake 构建 / ctest |
| `just dev-web` / `dev-dashboard` | 前端管理后台 / 大屏看板 dev server |
| `just iot-sim` | IoT 模拟器（无需硬件，直发 MQ） |
| `just perf-m1` | M1 性能基线（需 k6 CLI） |

## 约定 (必读)

1. **时间一律 UTC ISO8601 带 `Z` (`2026-08-09T12:36:51Z`), DB 用 `TIMESTAMPTZ`, 禁止输出本地偏移。
2. **统一响应** `{code,message,data,timestamp,trace_id}`, 错误 JSON 只能由全局错误拦截器产出。
3. **fail-closed 权限**: 新增路由必须在 `mes-backend/src/middlewares/perm_routes.cc` 注册权限映射, CI 门禁强制。
4. **MQ 只走 Outbox**: 事务内禁止直接发 MQ, 全项目唯一入口 `OutboxService::enqueue()` (同事务写 `mq_outbox`)。
5. **Service 层协程**: Drogon Service 用 C++20 协程写事务逻辑, 回调只允许出现在底层插件。
6. **迁移只进不退**: 生产环境只允许 up; down 脚本仅 dev 使用; 破坏性变更按 expand/contract 执行 (见 CONTRIBUTING.md)。

## 文档导航

| 文档 | 用途 / 受众 |
|------|------|
| [docs/README.md](docs/README.md) | **文档总索引**（本表之外所有文档的导航与状态） |
| [docs/MES_Architecture_Design.md](docs/MES_Architecture_Design.md) | 完整架构设计（事实源） |
| [docs/DEV_GUIDE.md](docs/DEV_GUIDE.md) | 开发维护指南（结构/规范/调试/踩坑） |
| [docs/DEPLOY_LINUX.md](docs/DEPLOY_LINUX.md) | Linux Ubuntu 24.04 部署完整手册 |
| [docs/PAD_MANUAL.md](docs/PAD_MANUAL.md) | 手持 PAD 扫码接入 / 日常操作手册 |
| [docs/FEATURE_INVENTORY.md](docs/FEATURE_INVENTORY.md) | 已有功能清单 + 核心缺口分析 |
| [docs/GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md) | 功能缺失详细分析（源码审计） |
| [docs/CORE_PLAN.md](docs/CORE_PLAN.md) | 核心功能完善方案（P1–P3 已完成） |
| [docs/P4_IMPLEMENTATION_PLAN.md](docs/P4_IMPLEMENTATION_PLAN.md) | P4 缺失功能实施方案（实施中） |
| [docs/BUILD_PROGRESS.md](docs/BUILD_PROGRESS.md) | 构建与验证进展记录 |
| [HANDOVER.md](HANDOVER.md) | 项目交接文档（新接手者必读） |
| [docs/adr/0001-authforge-integration.md](docs/adr/0001-authforge-integration.md) | ADR 0001: AuthForge 集成决策 |

## 开发阶段

| 阶段 | 内容 | 出口门禁 |
|------|------|----------|
| S0 | 预研 Spike (见 spike/) | 全链路 POC + ADR 0001 |
| M0 | 工程骨架 + CI | healthz 全绿 + CI 全绿 |
| M1 | 用户权限(A域) + 生产管理(B域) | `tests/e2e/m1_flow.ps1` + k6 P95<300ms |
| M2 | IoT / 质量 / 看板 / 集成 | 投毒消息进 DLQ + WS<2s + 复合压测 |
| M3 | 高可用与容量验证 | 2h 全链路压测不达标禁 GA |
