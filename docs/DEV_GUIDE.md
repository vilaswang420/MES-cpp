# HMS 开发维护指南

> 版本: 1.0 | 日期: 2026-08-15 | 适用: HMS M3 定稿代码库

---

## 目录

1. [项目全景](#1-项目全景)
2. [开发环境搭建](#2-开发环境搭建)
3. [代码结构与分层架构](#3-代码结构与分层架构)
4. [后端开发规范 (C++ Drogon)](#4-后端开发规范-c-drogon)
5. [前端开发规范 (React + Vue3)](#5-前端开发规范-react--vue3)
6. [数据库迁移规范](#6-数据库迁移规范)
7. [路由与权限规范](#7-路由与权限规范)
8. [MQ 消息规范](#8-mq-消息规范)
9. [WebSocket 规范](#9-websocket-规范)
10. [测试规范](#10-测试规范)
11. [构建与 CI 门禁](#11-构建与-ci-门禁)
12. [调试技巧](#12-调试技巧)
13. [常见开发任务](#13-常见开发任务)
14. [踩坑速查 (43 条)](#14-踩坑速查-43-条)

---

## 1. 项目全景

### 1.1 Monorepo 结构

```
New-HMS/
├── hms-backend/             # C++ Drogon 后端 (REST + WS + MQ 消费)
│   ├── src/
│   │   ├── controllers/      # HTTP 路由层 (10 个 Controller)
│   │   ├── services/         # 业务逻辑层 (8 个 Service)
│   │   ├── mq/               # MQ 消费/生产 (6 个模块)
│   │   ├── websocket/        # WS 广播管理
│   │   ├── middlewares/      # 横切关注点 (JWT/RBAC/Audit/Trace)
│   │   ├── metrics/          # Prometheus 指标采集
│   │   ├── common/           # ApiResponse, SqlParam
│   │   ├── models/           # 工单状态机
│   │   └── utils/            # JWT, Crypto, CircuitBreaker, DataScope
│   ├── config/               # Drogon 配置 (dev/prod/b/c)
│   ├── migrations/           # 数据库迁移 (9 套 up/down)
│   ├── vcpkg.json            # C++ 依赖清单
│   └── CMakeLists.txt
├── hms-web/                  # React 18 管理后台
│   ├── src/
│   │   ├── pages/            # 8 个功能页面
│   │   ├── layouts/          # 主布局 (菜单按权限动态过滤)
│   │   ├── store/            # 认证状态
│   │   └── utils/            # HTTP 请求封装
│   └── vite.config.ts
├── hms-dashboard/            # Vue3 大屏看板
│   ├── src/
│   │   ├── App.vue           # 三频道布局
│   │   └── composables/      # useChannel (WS 订阅 + 降级)
│   └── vite.config.ts
├── hms-iot/                  # IoT 采集服务 (C++ 骨架)
├── contracts/                # JSON Schema 契约 (3 个)
├── deploy/                   # Docker 编排 + Nginx + Prometheus
├── scripts/                  # 工具脚本 (12 个)
├── tests/                    # E2E 测试 (6 个)
├── perf/                     # k6 压测脚本
├── docs/                     # 文档
├── Justfile                  # 统一构建入口
├── CONTRIBUTING.md           # 贡献指南
└── HANDOVER.md               # 交接文档 (含 43 条踩坑)
```

### 1.2 技术栈速查

| 子系统 | 技术 | 版本 | 入口 |
|--------|------|------|------|
| 后端 | C++ Drogon | 1.9.13 | `hms-backend/src/main.cc` |
| 管理后台 | React + TS + Vite + AntD | 18.3 / 5.4 / 5.20 | `hms-web/src/main.tsx` |
| 大屏看板 | Vue3 + TS + Vite + ECharts | 3.4 / 5.5 | `hms-dashboard/src/main.ts` |
| IoT 采集 | C++ (SimpleAmqpClient) | C++20 | `hms-iot/src/main.cc` |
| 数据库 | PostgreSQL | 16 (分区+pg_partman+pg_cron) | — |
| 缓存 | Redis | 7 (生产 Cluster) | — |
| 消息队列 | RabbitMQ | 3.13 (生产 3 节点集群) | — |

### 1.3 开发阶段里程碑

| 阶段 | 内容 | 状态 |
|------|------|------|
| S0-S5 | 预研 + 工具链 + 首次编译 + 中间件联调 | ✅ 完成 |
| M0 | 工程骨架 + CI | ✅ 完成 |
| M1 | 用户权限 + 生产管理 | ✅ 完成 (k6 P95=284ms) |
| M2 | IoT / 质量 / 看板 / 集成 | ✅ 完成 (复合压测 P95=278ms) |
| M3 | 高可用 + 可观测性 + 发布演练 | ✅ 完成 |

---

## 2. 开发环境搭建

### 2.1 前置依赖

| 工具 | 版本 | 说明 |
|------|------|------|
| Docker Desktop | 最新 | 运行 PG/Redis/RMQ 容器 |
| CMake | 3.28+ | 后端构建 |
| vcpkg | 最新 (锁定 builtin-baseline) | C++ 依赖管理 |
| VS2022 Build Tools | MSVC 14.44 | Windows 编译器 (Linux 用 g++ 13+) |
| Node.js | 20+ | 前端构建 |
| Python | 3.10+ | 工具脚本 |
| just | 最新 | 统一命令入口 (可选) |
| golang-migrate | 4.17+ | 数据库迁移 |

### 2.2 Windows 开发环境

```powershell
# 1. 补 PATH (docker/just/migrate 不在 PATH 时)
$env:PATH = 'C:\Users\vilas\AppData\Local\Programs\DockerDesktop\resources\bin;E:\Work\Development\Tools\bin;' + $env:PATH

# 2. 设置 vcpkg
$env:VCPKG_ROOT = 'E:\Work\Development\Tools\vcpkg'

# 3. 启动中间件
docker compose -f deploy/compose/docker-compose.dev.yml up -d --build
docker ps --format "table {{.Names}}\t{{.Status}}"

# 4. 运行迁移
$mig = ((Get-Location).Path -replace '\\','/') + '/hms-backend/migrations'
migrate -path $mig -database 'postgres://hms:hms_dev_pwd@localhost:5432/hms?sslmode=disable' up

# 5. 编译后端
cmake -S hms-backend -B hms-backend/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build hms-backend/build --config Release -j

# 6. 运行测试
ctest --test-dir hms-backend/build -C Release --output-on-failure

# 7. 启动后端
./hms-backend/build/Release/hms-backend.exe hms-backend/config/drogon_config.json

# 8. 启动前端 (新终端)
cd hms-web; npm install; npm run dev      # → localhost:5173
cd hms-dashboard; npm install; npm run dev # → localhost:5174
```

### 2.3 Linux 开发环境

```bash
# 安装依赖
sudo apt install -y build-essential cmake ninja-build git curl \
    pkg-config libssl-dev zlib1g-dev nodejs npm

# vcpkg
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg

# 其余步骤同 Windows, 但路径用正斜杠
```

### 2.4 一键启动 (Justfile)

```bash
just dev-up          # 起中间件 + 迁移
just build-backend   # 编译后端
just test-backend    # 单测
just dev-web         # 前端开发服务器
just dev-dashboard   # 看板开发服务器
just check-perm-map  # 权限映射门禁
just e2e-m1          # M1 E2E 测试
just perf-m1         # k6 压测
```

### 2.5 开发环境凭据

| 服务 | 连接串 |
|------|--------|
| PostgreSQL | `postgres://hms:hms_dev_pwd@localhost:5432/hms` |
| Redis | `localhost:6379` |
| RabbitMQ | `amqp://hms:hms_dev_pwd@localhost:5672/` (管理台 15672) |
| 后端 | `http://localhost:8088` |
| 管理后台 | `http://localhost:5173` (Vite dev proxy → 8088) |
| 大屏看板 | `http://localhost:5174` (Vite dev proxy → 8088) |
| 默认账号 | `admin / password` |

---

## 3. 代码结构与分层架构

### 3.1 后端分层

```
HTTP 请求 → Controller → Service → DB/Redis/MQ
                ↑              ↑
            Middleware     Model/Util
```

| 层 | 目录 | 职责 | 规范 |
|----|------|------|------|
| Controller | `src/controllers/` | HTTP 路由、参数校验、响应构造 | 只做协议转换, 不含业务逻辑 |
| Service | `src/services/` | 业务逻辑、事务管理 | 用 Drogon 协程, 事务内调 OutboxService |
| MQ | `src/mq/` | 消息生产/消费 | 消费者必须幂等, 生产只走 Outbox |
| WebSocket | `src/websocket/` | WS 广播 | Redis Pub/Sub 跨实例 |
| Middleware | `src/middlewares/` | 横切关注点 | Trace → JWT → RBAC → Audit AOP 链 |
| Common | `src/common/` | 基础设施 | ApiResponse, SqlParam |
| Utils | `src/utils/` | 工具类 | JWT, Crypto, CircuitBreaker, DataScope |

### 3.2 后端 Controller 清单

| Controller | 文件 | 覆盖域 |
|-----------|------|--------|
| HealthController | `HealthController.cc` | `/healthz`, `/metrics` |
| AuthController | `AuthController.cc` | 登录/登出/刷新/密码/验证码 |
| UserController | `UserController.cc` | 用户 CRUD + 角色分配 |
| RoleController | `RoleController.cc` | 角色 CRUD + 权限授权 |
| DeptController | `DeptController.cc` | 部门树 CRUD |
| ProductionController | `ProductionController.cc` | 工单/产线/工艺/产品/计划 |
| WorkOrderController | `WorkOrderController.cc` | 工单状态机 + 报工 |
| IotController | `IotController.cc` | 设备/传感器/告警/采集任务 |
| QualityController | `QualityController.cc` | 检验标准/记录/缺陷 |
| IntegrationController | `IntegrationController.cc` | ERP/WMS 集成 |
| WsController | `WsController.cc` | WebSocket 升级 + 频道订阅 |
| MetricsController | `MetricsController.cc` | Prometheus 指标端点 |

### 3.3 统一响应信封

所有 REST API 必须返回统一格式:

```json
{
    "code": 200,
    "message": "success",
    "data": { },
    "timestamp": "2026-08-09T20:36:51Z",
    "trace_id": "a1b2c3d4e5f6"
}
```

- 成功: `ApiResponse::ok(data, traceId)`
- 错误: 只能由全局错误拦截器产出, 业务代码抛 `ApiException(code, message)`
- 时间戳: UTC ISO 8601 带 `Z`, 禁止本地偏移

### 3.4 前端页面清单

**hms-web (React 管理后台)**:

| 页面 | 路由 | 功能 |
|------|------|------|
| 登录 | `/login` | 账密登录, JWT 持久化 |
| 工单管理 | `/production/work-orders` | CRUD + 8 态状态机 + 报工 + 工序 |
| 生产计划 | `/production/plans` | 列表 + 新建 |
| 用户管理 | `/system/users` | CRUD + 启停 + 重置密码 + 角色分配 |
| 角色管理 | `/system/roles` | CRUD + 权限授权 + 5 档数据范围 |
| 部门管理 | `/system/departments` | 树形 CRUD |
| 权限管理 | `/system/permissions` | 只读权限树 |
| 审计日志 | `/system/audit-logs` | 分页只读查询 |

**hms-dashboard (Vue3 大屏)**:

| 功能 | 说明 |
|------|------|
| 三频道实时推送 | production.realtime / device.status / alert |
| WS 鉴权 | query token (浏览器 WS 无法带 Authorization 头) |
| 降级策略 | 连续 3 次重连失败 → REST 10s 轮询, 恢复后自动回切 |
| 连接状态指示 | 绿色 (正常) / 红色 (断开) / 黄色 (降级) |

---

## 4. 后端开发规范 (C++ Drogon)

### 4.1 新增 API 接口

**完整步骤** (以新增 "设备维护记录" 接口为例):

1. **Controller 声明** — 在对应 Controller 中添加路由:

```cpp
// src/controllers/IotController.cc
// 使用 ADD_METHOD_TO 宏注册路由
ADD_METHOD_TO(IotController::createMaintenance,
              "/api/v1/iot/devices/{id}/maintenance",
              Post,
              "hms::JwtMiddleware",      // JWT 鉴权
              "hms::RbacMiddleware",      // RBAC 权限检查
              "hms::AuditMiddleware");    // 操作审计
```

2. **权限映射注册** — 在 `src/middlewares/perm_routes.cc` 中注册:

```cpp
// 必须注册, 否则 CI 门禁失败 (fail-closed)
add("/api/v1/iot/devices/{id}/maintenance", "POST", "iot:device:maintenance");
```

3. **权限种子数据** — 在迁移中添加 `sys_permissions` 记录:

```sql
-- migrations/010_iot_maintenance_perm.up.sql
INSERT INTO sys_permissions (parent_id, perm_code, perm_name, perm_type, path, method)
VALUES (10, 'iot:device:maintenance', '设备维护记录', 3, '/api/v1/iot/devices/{id}/maintenance', 'POST');
```

4. **Service 层实现** — 用 Drogon 协程:

```cpp
// src/services/IotService.hh
drogon::Task<drogon::HttpResponsePtr> createMaintenance(
    int64_t deviceId, const nlohmann::json& body, int64_t userId);
```

5. **验证** — 运行权限映射检查:

```bash
python scripts/check_perm_mapping.py
```

### 4.2 Drogon 协程规范

```cpp
// ✅ 正确: Service 层用协程写事务
drogon::Task<Json::Value> MyService::doSomething(int64_t id) {
    auto trans = co_await db->newTransactionCoro();
    try {
        // 业务 SQL...
        co_await trans->execSqlCoro("UPDATE ...");

        // MQ 消息通过 Outbox 投递 (事务内禁止直接发 MQ)
        co_await OutboxService::enqueue(trans, "iot.exchange",
            "cmd.dev." + std::to_string(deviceId), payload);

        // 显式提交并等待 (drogon Transaction 析构才异步 COMMIT)
        co_await commitAwait(std::move(trans));

        co_return result;
    } catch (const std::exception& e) {
        // drogon 自动 rollback
        throw hms::ApiException(500, e.what());
    }
}
```

### 4.3 SQL 参数绑定

```cpp
// ✅ 正确: 数值/布尔用 SqlArg 文本绑定
auto result = co_await trans->execSqlCoro(
    "SELECT * FROM prod_work_orders WHERE status = $1 AND line_id = $2",
    hms::SqlArg(status),    // smallint 列: 文本绑定避免二进制格式不匹配
    hms::SqlArg(lineId));

// ❌ 错误: 裸传数值 (drogon 按 C++ 字节宽度二进制发送, smallint 列报错)
auto result = co_await trans->execSqlCoro(
    "SELECT * FROM prod_work_orders WHERE status = $1",
    status);  // int=4B, smallint=2B → incorrect binary data format
```

### 4.4 代码风格

- C++20, clang-format (配置见 `hms-backend/.clang-format`)
- Service 层用 Drogon 协程, 回调只允许出现在底层插件
- 时间一律 UTC ISO 8601 带 `Z`
- 错误 JSON 只允许全局错误拦截器产出, 业务代码抛 `ApiException`
- 路径类局部变量一律显式 `std::string` (避免 `auto` 推导 `const char*` 导致指针加法)
- Windows `max` 宏污染下 `std::max` 要写 `(std::max)`
- 含 mutex 的类不可拷贝/移动, 用 `unique_ptr` 懒创建

---

## 5. 前端开发规范 (React + Vue3)

### 5.1 React (hms-web)

**技术约定**:
- React 18.3 + TypeScript 5.5 + Vite 5.4 + Ant Design 5.20
- 路由: HashRouter (无需 Nginx history 模式配置)
- 状态: React Context (AuthProvider), 无全局状态库
- HTTP: `utils/request.ts` 统一封装, 自动处理 401 跳登录

**新增页面步骤**:

1. 在 `src/pages/` 下创建组件
2. 在 `src/App.tsx` 中注册路由
3. 在 `src/layouts/MainLayout.tsx` 中添加菜单项 (需有对应权限码)
4. 使用 `useAuth()` 获取用户信息和权限判断

```tsx
// 示例: 使用权限控制按钮
import { useAuth } from '../store/auth';

function MyPage() {
    const { hasPerm } = useAuth();
    return (
        <div>
            {hasPerm('iot:device:add') && <Button>新增设备</Button>}
        </div>
    );
}
```

**HTTP 请求**:

```tsx
import { http } from '../utils/request';

// GET
const data = await http.get('/api/v1/iot/devices', { page: 1, size: 20 });

// POST
const result = await http.post('/api/v1/iot/devices', { device_code: 'CNC-001', ... });

// 统一响应: { code, message, data, timestamp, trace_id }
// 401 自动跳转登录页
```

### 5.2 Vue3 (hms-dashboard)

**技术约定**:
- Vue 3.4 + TypeScript 5.5 + Vite 5.4
- ECharts 5.5 (已安装, 待集成)
- WebSocket: `composables/useChannel.ts`

**WebSocket 使用**:

```typescript
import { useChannel } from './composables/useChannel';

const { status, subscribe, unsubscribe } = useChannel();

// 订阅频道
onMounted(() => {
    subscribe(['production.realtime', 'device.status', 'alert.active']);
});

// 降级策略自动处理: WS 断开 3 次自动切 REST 轮询
```

---

## 6. 数据库迁移规范

### 6.1 迁移规则

- **只进不退**: 生产环境只允许 `up`, `down` 仅用于本地开发回退
- **expand/contract**: 破坏性变更拆三步:
  1. **expand**: 新增列/表/索引 (向后兼容)
  2. **migrate**: 应用层双写/回填, 切换读路径
  3. **contract**: 确认无流量后删除旧列/表
- 分区表变更需验证 pg_partman 注册不受影响
- 新增分区表需在 `scripts/test-migrate-roundtrip.ps1` 补跨分区插入用例

### 6.2 创建新迁移

```bash
# 迁移文件命名: {序号}_{描述}.{up|down}.sql
# 例: 010_add_maintenance_tables.up.sql / 010_add_maintenance_tables.down.sql

# 迁移路径必须用正斜杠
migrate -path hms-backend/migrations \
    -database "postgres://hms:hms_dev_pwd@localhost:5432/hms?sslmode=disable" up

# 回退一步 (仅开发)
migrate -path hms-backend/migrations \
    -database "postgres://hms:hms_dev_pwd@localhost:5432/hms?sslmode=disable" down 1

# 往返测试
just migrate-roundtrip  # 或: powershell -File scripts/test-migrate-roundtrip.ps1
```

### 6.3 现有迁移清单

| 序号 | 文件 | 内容 |
|------|------|------|
| 001 | auth_tables | 部门/用户/角色/权限/关联/审计日志 (7 表) |
| 002 | seed | 超管 + 7 角色 + 76 权限 + 默认部门 |
| 003 | prod_tables | 产品/产线/工位/工艺/工单/工序/计划 (8 表) |
| 004 | iot_tables | 设备类型/设备/传感器/原始数据/告警/采集任务 (6 表) |
| 005 | qc_tables | 检验标准/检验项目/检验记录/缺陷 (4 表) |
| 006 | integ_tables | API配置/ERP订单/WMS库存/同步日志 (4 表) |
| 007 | iot_perm_seed | IoT 域权限码种子 |
| 008 | qc_perm_seed | 质量域权限码种子 |
| 009 | integ_perm_seed | 集成域权限码种子 |

### 6.4 数据库陷阱

- pg_partman 5.x 分区后缀固定 `_pYYYYMMDD`, 手工预建分区必须对齐
- DO 块内禁止复用外层 `$$` 标签, 用 `$cron$` 等唯一标签
- 列名避开 PG 保留字 (`offset`/`user`/`order` 等)
- `hms` 库被 pg_cron worker 占用时无法 `DROP DATABASE`, 重置用 `DROP SCHEMA public CASCADE`
- 数据卷若先于定制镜像初始化, 需手工 `CREATE EXTENSION pg_partman/pg_cron`

---

## 7. 路由与权限规范

### 7.1 fail-closed 模型

```
新增路由 → perm_routes.cc 注册 → 迁移补 sys_permissions → CI 门禁检查
```

- **唯一事实源**: `src/middlewares/perm_routes.cc`
- **CI 门禁**: `scripts/check_perm_mapping.py` 扫描所有 `ADD_METHOD_TO` 声明, 缺失注册即构建失败
- **公开接口**: login/captcha/healthz/metrics/ws 必须显式列入白名单

### 7.2 权限类型

| 类型 | perm_type | 说明 |
|------|-----------|------|
| 菜单 | 1 | 前端菜单可见性 |
| 按钮 | 2 | 页面内操作按钮 |
| 接口 | 3 | 后端 API 权限码 |

### 7.3 数据范围

| data_scope | 含义 | SQL 注入 |
|-----------|------|---------|
| 1 | 仅本人 | `created_by = userId` |
| 2 | 本部门 | `dept_id = userDeptId` |
| 3 | 本部门及子部门 | 递归 CTE 下钻 |
| 4 | 全部 | `1=1` |
| 5 | 自定义 | `dept_id IN (SELECT ...)` |

多角色合并: 取最宽范围 (4 > 3 > 2 > 1), 自定义角色部门集取并集。

---

## 8. MQ 消息规范

### 8.1 核心原则

1. **事务内禁止直接发 MQ** — 唯一入口 `OutboxService::enqueue()` (同事务写 `mq_outbox`)
2. 消息体必须符合 `contracts/` 下 JSON Schema, 必含 `version` 字段
3. 消费者必须处理 `x-retry-count` 头: 超过 3 次 nack 进 DLQ
4. 消费者必须幂等

### 8.2 RabbitMQ 拓扑

```
iot.exchange (topic)
├── data.#           → iot.data.queue            (数据入库消费)
├── alert.#          → iot.alert.queue           (告警处理消费)
├── cmd.stop_collection → iot.cmd.queue          (后端 StopCollectionHandler 二次投递, 精确 key 防回环)
├── cmd.stop.#       → iot.cmd.collector.queue   (停采指令, hms-iot 独占消费)
├── cmd.dev.#        → iot.cmd.collector.queue   (设备指令, hms-iot 独占消费)
└── retry.data       → iot.retry.queue           (TTL 10s → 回 iot.exchange)

iot.dlx (fanout)
└── (all)     → iot.dlq           (死信最终归宿)
```

### 8.3 消息契约 (IoT 数据上报)

```json
{
    "version": "1.0",
    "device_id": 10,
    "device_code": "CNC-001",
    "sensor_id": 42,
    "value": 36.5,
    "quality": 192,
    "ts": "2026-08-09T12:36:51Z",
    "task_id": 3
}
```

兼容性: 只增不删字段, 消费者忽略未知字段, 破坏性变更升级 `version`。

### 8.4 MQ 运维

```bash
# 检查拓扑一致性
python scripts/check_mq_topology.py

# 幂等补建缺失绑定
python scripts/apply_mq_topology.py

# 查看队列状态
docker exec hms-rabbitmq rabbitmqctl list_queues name messages consumers

# 清空 DLQ (谨慎)
docker exec hms-rabbitmq rabbitmqctl purge_queue iot.dlq
```

---

## 9. WebSocket 规范

### 9.1 连接

```
wss://hms.example.com/ws/dashboard?token=JWT_TOKEN
```

- 浏览器 WS 无法带 Authorization 头, 用 query token 鉴权
- token 在 WsController 中严格校验 (fail-closed)

### 9.2 订阅消息

```json
{
    "action": "subscribe",
    "channels": ["production.realtime", "device.status", "alert.active"]
}
```

### 9.3 推送信封

```json
{
    "version": "1.0",
    "channel": "production.realtime",
    "ts": "2026-08-09T20:36:51Z",
    "payload": { ... }
}
```

> **注意**: 信封无 `type` 字段 (`contracts/ws-push.schema.json` additionalProperties=false), 客户端过滤推送只能按 `channel` 判。

### 9.4 降级策略

WS 连续重连失败 ≥ 3 次 → 切 REST 10s 轮询, WS 恢复后自动回切。

### 9.5 跨实例广播

- Redis Pub/Sub 订阅 + 200ms 合并窗口 + 1Hz realtime 生产者
- realtime 生产者 leader 选举 (Redis 租约 `ws:realtime:leader`)
- 双实例广播: 客户端连任一实例均收到推送

---

## 10. 测试规范

### 10.1 测试体系

| 类型 | 工具 | 位置 | 运行命令 |
|------|------|------|---------|
| 单元测试 | CTest | `hms-backend/tests/` | `ctest --test-dir hms-backend/build -C Release` |
| E2E (M1) | PowerShell | `tests/e2e/m1_flow.ps1` | `just e2e-m1` |
| 并发超报 | PowerShell | `tests/e2e/concurrent_report.ps1` | `just e2e-concurrent-report` |
| M2 冒烟 | PowerShell | `tests/e2e/m2_*.ps1` | 手动执行 |
| 压测 (M1) | k6 | `perf/k6/m1_baseline.js` | `just perf-m1` |
| 压测 (M2) | k6 | `perf/k6/m2_composite.js` | 手动执行 |
| 权限映射 | Python | `scripts/check_perm_mapping.py` | `just check-perm-map` |
| 迁移往返 | PowerShell | `scripts/test-migrate-roundtrip.ps1` | `just migrate-roundtrip` |
| MQ 拓扑 | Python | `scripts/check_mq_topology.py` | 手动执行 |

### 10.2 出口门禁标准

| 阶段 | 门禁 | 标准 |
|------|------|------|
| M1 | E2E | 全流程通过 (登录→建单→报工→完工→审计) |
| M1 | 并发超报 | 8 路并发恰好 1 成功 + 7 个 409 |
| M1 | 权限映射 | 76 条路由全注册 |
| M1 | 单测 | 26/26 通过 |
| M1 | k6 基线 | P95 < 300ms, failed < 0.5%, api_err < 0.5% |
| M2 | IoT 入库 | 1 万条 5s 内全量入库 + 毒消息进 DLQ |
| M2 | WS 延迟 | P95 < 2s |
| M2 | WS 连接 | 1000 连接存活全程 |
| M2 | 复合压测 | P95 劣化 < 30% (对比基线) |
| M3 | 蓝绿发布 | 五阶段五门禁全过 |

---

## 11. 构建与 CI 门禁

### 11.1 后端构建

```bash
# Windows (MSVC)
cmake -S hms-backend -B hms-backend/build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build hms-backend/build --config Release -j

# Linux (GCC)
cmake -S hms-backend -B hms-backend/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build hms-backend/build -j$(nproc)

# Docker (生产镜像)
docker build -f deploy/backend/Dockerfile -t hms-backend:latest .
```

### 11.2 前端构建

```bash
# 管理后台
cd hms-web && npm ci && npm run build    # → dist/

# 大屏看板 (注意子路径)
cd hms-dashboard && npm ci && npx vite build --base=/dashboard/  # → dist/
```

### 11.3 CI 门禁清单

每个 PR 必须通过:

1. ✅ 编译 (CMake + MSVC/GCC)
2. ✅ 单测 (CTest)
3. ✅ clang-format 检查
4. ✅ 迁移往返测试
5. ✅ 权限映射检查 (`check_perm_mapping.py`)
6. ✅ 契约 schema 校验 (JSON Schema)

---

## 12. 调试技巧

### 12.1 后端调试

```bash
# 健康检查
curl http://localhost:8088/healthz

# 查看 Prometheus 指标
curl http://localhost:8088/metrics | grep hms_

# 查看 MQ 积压
docker exec hms-rabbitmq rabbitmqctl list_queues name messages consumers

# 查看 outbox 待投递
docker exec hms-postgres psql -U hms -d hms -c \
    "SELECT count(*), min(created_at) FROM mq_outbox WHERE status = 0;"

# 查看 Redis 缓存
docker exec hms-redis redis-cli keys "perm:*" | head
docker exec hms-redis redis-cli get "perm:user:1"
docker exec hms-redis redis-cli get "device:latest:10"

# WS leader 租约
docker exec hms-redis redis-cli get "ws:realtime:leader"
```

### 12.2 双实例调试

```bash
# 启动双实例 (8088/8089)
powershell -File scripts/start_dual_instances.ps1

# 实例 A (直连 PG 5432)
./hms-backend/build/Release/hms-backend.exe hms-backend/config/drogon_config.json

# 实例 B (经 PgBouncer 6432)
./hms-backend/build/Release/hms-backend.exe hms-backend/config/drogon_config.b.json

# 验证跨实例 WS 广播
# 连 8088 的客户端应收到 8089 发布的推送
```

### 12.3 IoT 模拟器

```bash
# 发布 1 万条数据 (5s 内入库)
python scripts/iot_simulator.py --count 10000

# 投毒消息 (验证有界重试 → DLQ)
python scripts/iot_simulator.py --poison

# WS 负载测试
python scripts/ws_load.py --mode latency   # 延迟测试
python scripts/ws_load.py --mode load      # 1000 连接测试
```

### 12.4 ERP/WMS 桩服务

```bash
# 启动桩服务 (端口 9095)
python scripts/erp_wms_stub.py

# 故障注入
curl http://localhost:9095/__control -d '{"action":"fail_wms"}'
curl http://localhost:9095/__control -d '{"action":"fail_erp_orders"}'
curl http://localhost:9095/__control -d '{"action":"reset"}'
```

---

## 13. 常见开发任务

### 13.1 新增一个完整的 CRUD 模块

1. **数据库**: 创建迁移 `migrations/NNN_xxx_tables.up.sql` + down
2. **权限种子**: 创建迁移 `migrations/NNN_xxx_perm_seed.up.sql` + down
3. **Service**: 在 `src/services/` 实现 XxxService
4. **Controller**: 在 `src/controllers/` 实现 XxxController, 用 ADD_METHOD_TO 注册路由
5. **权限映射**: 在 `src/middlewares/perm_routes.cc` 注册每条路由
6. **前端**: 在 `hms-web/src/pages/` 创建页面, 注册路由, 添加菜单
7. **测试**: 编写冒烟测试
8. **验证**: `just check-perm-map` + 冒烟测试 + 迁移往返

### 13.2 新增 WebSocket 频道

1. 在 `WsBroadcastManager` 中注册新频道
2. 在 Redis Pub/Sub 中订阅
3. 更新 `contracts/ws-push.schema.json` 的 channel 枚举
4. 前端 `useChannel` 的 subscribe 中添加频道名

### 13.3 新增 MQ 消费者

1. 创建 `src/mq/XxxHandler.hh/.cc`
2. 在 `main.cc` 的 `registerBeginningAdvice` 中 `XxxHandler::start(mqCfg)`
3. 实现 `stop()` 在 `app().run()` 后调用
4. 消费者必须: 处理 `x-retry-count` + 幂等 + 手动 ACK

### 13.4 修改数据库表结构 (expand/contract)

```sql
-- Step 1: expand (新增列, 向后兼容)
ALTER TABLE prod_work_orders ADD COLUMN extra_info JSONB;

-- Step 2: migrate (应用层双写, 回填数据)
UPDATE prod_work_orders SET extra_info = '{}' WHERE extra_info IS NULL;

-- Step 3: contract (确认无流量后删除旧列, 需新迁移)
-- ALTER TABLE prod_work_orders DROP COLUMN old_column;
```

---

## 14. 踩坑速查 (43 条)

> 完整版见 `HANDOVER.md` 第七节, 此处列出高频踩坑。

### PowerShell 环境

1. 外层 Node fallback shell 吞 `$` 变量 → 写成 `.ps1` 文件用 `powershell -File` 执行
2. UTF-8 无 BOM 的中文 ps1 在 PS5.1 下被 GBK 误读 → 含中文必须 UTF-8 with BOM
3. 终端不支持 `&&`, 用 `;`

### 数据库 / 迁移

5. pg_partman 5.x 分区后缀固定 `_pYYYYMMDD`
6. DO 块内禁止复用 `$$` 标签
7. 列名避开 PG 保留字
8. migrate 的 `-path` 在 Windows 上必须正斜杠

### Drogon 运行时 (最易踩)

18. Redis `execCommandAsync` 是 C 变参: `%s` 必须传 `.c_str()`
19. 数值绑定参数一律用 `hms::SqlArg()` 文本化
20. 协程事务响应前必须 `co_await commitAwait(std::move(trans))`
21. `Task` 是惰性协程, fire-and-forget 必须用 `drogon::AsyncTask`
28. 字符串字面量在前的 `+` 遇 `auto` 推导 `const char*` 即指针加法

### M2 联调 (WS/IoT)

23. `WS_PATH_ADD` 宏展开不带尾分号, 多路径注册时手动补 `;`
24. hiredis Windows: 订阅连接禁止 `redisSetTimeout`
30. 工单状态流转动作接口是 PUT 不是 POST
31. MQ 绑定会丢, 拓扑声明与运行态必须双核对
32. WS 推送信封无 type 字段, 只能按 `channel` 过滤
33. 后端未启 CORS, 前端必须同源接入 (Vite proxy / Nginx 反代)

### M3 (PgBouncer/compose)

36. edoburu/pgbouncer 镜像容器内监听 5432, 端口映射 `-p 6432:5432`, 需 `AUTH_TYPE=scram-sha-256`
38. 工单列表响应字段是 `data.list` 不是 `items`

---

> **文档版本**: 1.0 | **最后更新**: 2026-08-15 | **维护者**: HMS 团队
