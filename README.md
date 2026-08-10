# HMS 制造执行系统

> 技术栈: C++ Drogon + React 18 / Vue 3 + PostgreSQL 16 + Redis 7 + RabbitMQ 3.13
> 设计事实源: [`docs/HMS_Architecture_Design.md`](docs/HMS_Architecture_Design.md)

## 仓库结构

```
New-HMS/
├── docs/                    # 设计文档 + adr/ 决策记录 + spike 报告
├── hms-backend/             # Drogon C++ 后端 (REST + WS + MQ 消费)
├── hms-web/                 # React 18 + Vite + AntD 5 管理后台
├── hms-dashboard/           # Vue3 + ECharts 5 大屏看板
├── hms-iot/                 # 独立 C++ IoT 采集服务 (M2)
├── contracts/               # MQ/WS/错误响应 JSON Schema 单一事实源
├── deploy/                  # compose / 定制 PG 镜像 / MQ 拓扑 / nginx
├── perf/k6/                 # 压测脚本 (阶段出口硬门禁)
├── scripts/                 # migrate 往返测试 / 权限映射检查 / IoT 模拟器
├── spike/                   # 一次性 POC (不进主干)
└── tests/                   # E2E 脚本
```

## 快速开始 (M0 DoD)

前置依赖: Docker、[just](https://github.com/casey/just)、[golang-migrate](https://github.com/golang-migrate/migrate)、Node 18+、Python 3.10+。

```bash
just dev-up        # compose up + migrate up
# 启动后端 (需 vcpkg + CMake, 见 hms-backend/README.md)
cmake -S hms-backend -B hms-backend/build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build hms-backend/build -j
./hms-backend/build/hms-backend hms-backend/config/drogon_config.json
# 验证四服务
curl http://localhost:8088/healthz
```

默认超管: `admin / password` (仅开发环境, 见 `hms-backend/migrations/002_seed.up.sql`)。

## 约定 (必读)

1. **时间一律 UTC ISO8601 带 `Z` (`2026-08-09T12:36:51Z`), DB 用 `TIMESTAMPTZ`, 禁止输出本地偏移。
2. **统一响应** `{code,message,data,timestamp,trace_id}`, 错误 JSON 只能由全局错误拦截器产出。
3. **fail-closed 权限**: 新增路由必须在 `hms-backend/src/middlewares/perm_routes.cc` 注册权限映射, CI 门禁强制。
4. **MQ 只走 Outbox**: 事务内禁止直接发 MQ, 全项目唯一入口 `OutboxService::enqueue()` (同事务写 `mq_outbox`)。
5. **Service 层协程**: Drogon Service 用 C++20 协程写事务逻辑, 回调只允许出现在底层插件。
6. **迁移只进不退**: 生产环境只允许 up; down 脚本仅 dev 使用; 破坏性变更按 expand/contract 执行 (见 CONTRIBUTING.md)。

## 开发阶段

| 阶段 | 内容 | 出口门禁 |
|------|------|----------|
| S0 | 预研 Spike (见 spike/) | 全链路 POC + ADR 0001 |
| M0 | 工程骨架 + CI | healthz 全绿 + CI 全绿 |
| M1 | 用户权限(A域) + 生产管理(B域) | `tests/e2e/m1_flow.ps1` + k6 P95<300ms |
| M2 | IoT / 质量 / 看板 / 集成 | 投毒消息进 DLQ + WS<2s + 复合压测 |
| M3 | 高可用与容量验证 | 2h 全链路压测不达标禁 GA |
