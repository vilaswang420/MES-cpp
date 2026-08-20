# MES 功能缺失分析报告

> 版本: 1.0 | 日期: 2026-08-15 | 基于: 全量源码逐文件审计 (提交至 da6ff0a)
> 审计范围: 13 个后端 Controller、8 个 Service、9 组迁移、前端 8 页面、大屏看板、IoT 服务、部署配置、脚本、测试
>
> ⚠️ **状态更新（2026-08-20）**：本报告为 P4 实施前的功能缺失基线（提交 da6ff0a，2026-08-15）。**P4（5.1–5.7）已全部实现**（Modbus 真实采集、停采链路两端、大屏 ECharts、真 OEE 消费者、前端 3 模块、GA 2h 脚本、IoT 管理补齐），详见 `docs/P4_IMPLEMENTATION_PLAN.md`。因此**第 1 章"核心功能缺失项"大部分已被 P4 关闭**，仅 5.1 的 mes-iot 容器化（Dockerfile.iot / compose 服务）尚未补充。本报告的"缺失"表述请以 P4 落地后的实际状态为准。

---

## 目录

1. [核心功能缺失项 (阻碍生产使用)](#1-核心功能缺失项-阻碍生产使用)
2. [增强功能缺失项 (提升运营效率)](#2-增强功能缺失项-提升运营效率)
3. [技术债务 (质量/安全/性能)](#3-技术债务-质量安全性能)
4. [数据库索引与约束缺失](#4-数据库索引与约束缺失)
5. [设计文档漂移记录](#5-设计文档漂移记录)
6. [实施优先级矩阵](#6-实施优先级矩阵)

---

## 1. 核心功能缺失项 (阻碍生产使用) — *已由 P4(5.1–5.7) 关闭，见 P4_IMPLEMENTATION_PLAN.md*

> 以下缺口在 P4 实施前直接阻碍 MES 投入生产使用，现已由 P4(5.1–5.7) 关闭（IoT 真实采集 / 停采链路 / 大屏图表 / 真 OEE / 前端 3 模块 / IoT 管理均已实现）；仅 mes-iot 容器化部署（Dockerfile.iot / compose 服务）尚未补充。

### 1.1 IoT 真实采集完全缺失

| 维度 | 详情 |
|------|------|
| **影响** | 系统无法接入任何真实设备，所有 IoT 数据来自 Python 模拟器 |
| **位置** | `mes-iot/src/main.cc` L193-195 (显式 TODO) |
| **缺失内容** | Modbus TCP 轮询采集、OPC-UA 采集、MQTT 网桥、epoll 事件循环、worker 线程池 |
| **连带影响** | 生产 compose 无 IoT 容器定义，与"三系统独立部署"设计目标矛盾 |
| **工作量** | 大 (2-4 周，Modbus TCP 优先) |
| **建议** | 优先实现 Modbus TCP (覆盖 80% 工业设备)，复用现有 BatchPublisher + AMQP 链路 |

### 1.2 停采指令链路两端空转

| 维度 | 详情 |
|------|------|
| **影响** | 工单完工后应自动停止设备采集，实际两端均未实现，采集不会停止 |
| **位置** | 后端: `mes-backend/src/mq/StopCollectionHandler.cc` L48-52 (注释 "MVP 日志占位")；IoT 端: `mes-iot/src/main.cc` L195 (TODO) |
| **现状** | 后端收到停采指令后仅 LOG_INFO + ack，不做任何事；IoT 端无消费者 |
| **工作量** | 中 (3-5 天，两端各实现) |
| **建议** | 后端 StopCollectionHandler 实际下发 MQ 指令；IoT 端消费 `cmd.#` 队列暂停对应设备轮询 |

### 1.3 大屏看板无图表可视化

| 维度 | 详情 |
|------|------|
| **影响** | 大屏仅文字 JSON dump，不满足车间可视化需求 |
| **位置** | `mes-dashboard/src/App.vue` L115 (`<pre>{{ JSON.stringify(production) }}</pre>`)、L121 (`<li>{{ id }}: {{ JSON.stringify(d) }}</li>`) |
| **现状** | `package.json` 声明 `echarts ^5.5.1` 依赖但源码中零 import — 纯死依赖 |
| **缺失** | OEE 仪表盘、产线甘特图、设备拓扑图、质量趋势图、告警时间线 |
| **工作量** | 中 (1-2 周) |
| **建议** | 集成 ECharts，按优先级实现: 产线 OEE 仪表盘 → 设备状态拓扑 → 告警面板 → 质量趋势 |

### 1.4 前端 IoT/质量/集成 3 大模块页面空白

| 维度 | 详情 |
|------|------|
| **影响** | 后端 32 个接口已就绪 (IoT 18 + 质量 7 + 集成 7)，但前端无页面，运营人员无法操作 |
| **位置** | `mes-web/src/pages/` 下无 `iot/`、`quality/`、`integration/` 目录 |
| **缺失页面** | 设备管理、传感器管理、告警管理、采集任务管理 (IoT 4 页)；检验标准、检验记录、缺陷管理 (质量 3 页)；ERP/WMS 同步日志、重试 (集成 1-2 页) |
| **工作量** | 中 (2-3 周，按优先级: 设备 > 质量 > 集成) |
| **建议** | 复用现有 Ant Design Pro 组件模式，按后端 API 契约逐页开发 |

### 1.5 真实 OEE 计算缺失

| 维度 | 详情 |
|------|------|
| **影响** | 大屏推送的 OEE 为伪 OEE (good_qty/plan_qty)，非设计文档定义的 可用率×表现性×质量 |
| **位置** | `mes-backend/src/ws/WsBroadcastManager.cc` L110 (伪 OEE 计算) |
| **缺失** | OEE 自动计算消费者 (MQ 消费者)、可用率/表现性/质量分项计算 |
| **工作量** | 中 (1 周) |
| **建议** | 在 MQ 消费者中实现 OEE 计算，基于 iot_raw_data (设备运行时长) + prod_work_orders (产量) + qc_inspections (质量) |

### 1.6 验证码为明文返显

| 维度 | 详情 |
|------|------|
| **影响** | 登录无防机器人能力，验证码直接在响应体中返回明文 |
| **位置** | `mes-backend/src/services/AuthService.cc` L340-341 (注释 "MVP 直接返回明文 dev_captcha") |
| **现状** | 后端返回 `{"captcha_id":"dev_captcha","captcha_text":"明文验证码"}`，前端 Login.tsx L9 注释 "MVP dev 环境无验证码" |
| **工作量** | 小 (1-2 天) |
| **建议** | 后端实现 SVG/PNG 验证码渲染，前端登录页加验证码输入框 |

### 1.7 GA 标准环境容量验证未执行

| 维度 | 详情 |
|------|------|
| **影响** | 本机校准版通过 (k6 P95=278ms)，但标准服务器 2h 压测未执行，无法确认生产容量达标 |
| **位置** | `perf/k6/m2_composite.js` (脚本已就绪，未在标准环境执行) |
| **工作量** | 小 (1-2 天) |
| **建议** | 在标准服务器上执行 2h 复合压测，覆盖 WS 连接 + REST API + 报工并发 |

---

## 2. 增强功能缺失项 (提升运营效率)

> 以下缺口不阻碍上线，但影响运营效率和用户体验。

### 2.1 工单取消路由缺失

| 维度 | 详情 |
|------|------|
| **影响** | 状态机支持 `Event::Cancel` (WorkOrderService.cc L148-149)，但无 HTTP 路由暴露，API 不可达 |
| **位置** | `mes-backend/src/controllers/WorkOrderController.cc` L14-33 (有 schedule/release/start/pause/complete/close，无 cancel) |
| **工作量** | 小 (半天) |
| **建议** | 添加 `PUT /api/v1/production/work-orders/{id}/cancel` 路由 |

### 2.2 主数据 CRUD 不完整

| 维度 | 详情 |
|------|------|
| **影响** | 产线/产品/工艺/计划只有 Create + 只读 List，无编辑/删除；列表无分页 |
| **位置** | `mes-backend/src/controllers/ProductionController.cc` L17-28 |
| **具体缺失** | 产线无 PUT/DELETE；产品无 PUT/DELETE；工艺无 PUT/DELETE 且列表只查 `status=1` (草稿不可见)；计划无状态流转端点 (0 草稿/1 已确认/2 已执行/3 已取消)；工位无 CRUD；`plans` 硬编码 `LIMIT 100` (L146) |
| **工作量** | 中 (3-5 天) |
| **建议** | 补齐 PUT/DELETE 路由，添加分页参数，实现计划状态流转 |

### 2.3 用户/审计导入导出 + 配置刷新缺失

| 维度 | 详情 |
|------|------|
| **影响** | 批量用户管理需逐个创建；合规审计无法导出；配置更新后无法刷新缓存 |
| **位置** | 设计文档 4.3/4.11 节定义，代码未实现 |
| **缺失端点** | `POST /api/v1/system/users/import`、`GET /api/v1/system/users/export`、`GET /api/v1/system/audit-logs/export`、`POST /api/v1/system/configs/refresh` |
| **工作量** | 小 (2-3 天) |
| **建议** | 导入用 Excel 解析 (openxlsx/openpyxl)，导出用 CSV/Excel 流式写入 |

### 2.4 审计日志查询过滤不完整

| 维度 | 详情 |
|------|------|
| **影响** | 设计要求 operation/start_time/end_time 过滤，实现只有 user_id/module |
| **位置** | `mes-backend/src/services/SystemService.cc` L704-715 |
| **工作量** | 小 (半天) |
| **建议** | 补齐 SQL WHERE 条件和前端筛选表单 |

### 2.5 IoT 域管理功能不完整

| 维度 | 详情 |
|------|------|
| **影响** | 设备类型/传感器/告警管理功能缺失 |
| **位置** | `mes-backend/src/controllers/IotController.cc` |
| **具体缺失** | 设备类型 (`iot_device_types` 表) 无任何管理端点；传感器无 update/delete (只有 list/add, L21-22)；告警只支持 acknowledge (status 0→1)，无 resolved/ignored 处理 (表定义 status 2/3)；无告警自动消除/恢复逻辑；设备心跳超时离线判定未实现 (status 永远不会被置 0/2) |
| **工作量** | 中 (3-5 天) |
| **建议** | 补齐传感器 CRUD、告警状态机、设备类型管理；实现设备心跳超时自动离线 |

### 2.6 质量追溯链断裂

| 维度 | 详情 |
|------|------|
| **影响** | 缺陷处置不记录处置人；质检不联动报工流程 |
| **位置** | `mes-backend/src/services/QcService.cc` L376 (`(void)userId;` — 缺陷处置不记录处置人)；`WorkOrderService.cc` report (工序 quality_check 标志在报工流程中完全不校验) |
| **工作量** | 小 (1-2 天) |
| **建议** | QcService 缺陷处置落库 userId；报工时校验 quality_check=true 的工序必须有关联检验记录 |

### 2.7 PAD 移动端整体缺失

| 维度 | 详情 |
|------|------|
| **影响** | 操作员无法在工位扫码报工/质检，只能用 PC |
| **位置** | `docs/PAD_MANUAL.md` 有操作手册但无代码实现 |
| **工作量** | 大 (3-4 周) |
| **建议** | PWA 方案 (复用后端 API)，或 mes-web 移动适配 |

### 2.8 可观测性补齐

| 维度 | 详情 |
|------|------|
| **影响** | 告警规则已定义但无推送通道；指标无可视化 |
| **缺失** | Alertmanager (6 条告警规则发不出)、Grafana (指标无可视化看板)、日志聚合 (ELK/Loki) |
| **工作量** | 小 (Alertmanager+Grafana 各 1 天；日志聚合 1 周) |
| **建议** | 优先部署 Alertmanager + 邮件/钉钉通知，再部署 Grafana + 导入 Dashboard 模板 |

### 2.9 CI/CD Pipeline 缺失

| 维度 | 详情 |
|------|------|
| **影响** | CI 配置已修复 (本次提交)，但无 CD 自动部署流水线 |
| **位置** | `.github/workflows/ci.yml` (CI 门禁已修复) |
| **缺失** | 自动构建镜像、推送 registry、蓝绿部署自动化 |
| **工作量** | 中 (3-5 天) |
| **建议** | GitHub Actions CD job: build → push → ssh deploy → health check |

### 2.10 多看板配置 + WS 会话审计

| 维度 | 详情 |
|------|------|
| **影响** | 仅一个默认看板；WS 会话无审计记录 |
| **位置** | `sys_websocket_sessions` 表 (006 迁移 L81-95) 有 `dashboard_id` 字段但从未使用；WsController 从未写入此表 |
| **工作量** | 中 (1 周) |
| **建议** | 实现多看板配置 (前端路由 + 后端频道过滤)，WsController 写入会话审计 |

### 2.11 生产计划状态流转缺失

| 维度 | 详情 |
|------|------|
| **影响** | 计划创建后永远停在状态 0 (草稿)，无法确认/执行/取消 |
| **位置** | `mes-backend/src/controllers/ProductionController.cc` (无计划状态流转端点) |
| **工作量** | 小 (1 天) |
| **建议** | 添加 `PUT /api/v1/production/plans/{id}/confirm`、`/execute`、`/cancel` 路由 |

---

## 3. 技术债务 (质量/安全/性能)

> 以下问题不影响功能正确性，但影响代码质量、安全性和可维护性。

### 3.1 SQL 拼接安全面

| 维度 | 详情 |
|------|------|
| **风险** | 多处数值参数靠"先 clamp 再字符串拼接"构造 SQL，当前安全但脆弱 |
| **位置** | `IotService.cc` L109-112、`WorkOrderService.cc` L164-168、`SystemService.cc` L61-62 |
| **建议** | 迁移到 Drogon SqlParams 占位符 (`$1, $2, ...`)，消除字符串拼接 |

### 3.2 源码注释编码损坏

| 维度 | 详情 |
|------|------|
| **影响** | 多个文件中文注释显示为 `??????` |
| **位置** | `StopCollectionHandler.cc`、`WsBroadcastManager.cc`、`WsController.cc` |
| **建议** | 检查文件编码 (应为 UTF-8 无 BOM)，修复损坏注释 |

### 3.3 登录验证缓存安全风险

| 维度 | 详情 |
|------|------|
| **风险** | AuthService 登录验证缓存以 `password|hash` 明文作 key 存内存，密码驻留进程内存 60s |
| **位置** | `mes-backend/src/services/AuthService.cc` L24-50 |
| **建议** | 改用 `username|ip` 作 key，或直接移除缓存 (bcrypt 验证足够快) |

### 3.4 自签名证书私钥入库

| 维度 | 详情 |
|------|------|
| **风险** | `deploy/nginx/certs/mes.key` (私钥) 直接提交进仓库 |
| **建议** | 从仓库移除，加入 `.gitignore`，由部署脚本生成 |

### 3.5 监控覆盖不完整

| 维度 | 详情 |
|------|------|
| **影响** | Prometheus 仅抓 backend 指标，主机/中间件层无监控 |
| **缺失** | node-exporter (主机级)、postgres-exporter (DB 级)、redis-exporter (缓存级)、rabbitmq-exporter (MQ 级) |
| **建议** | 在 compose 中添加 4 个 exporter + 对应 Grafana 面板 |

### 3.6 测试覆盖不足

| 维度 | 详情 |
|------|------|
| **影响** | 8 个 Service 零单测；前端无任何测试 |
| **已覆盖** | 仅 5 个纯逻辑单测: bcrypt、CircuitBreaker、DataScopeFilter、JwtUtils、工单状态机 |
| **建议** | 优先补 WorkOrderService (报工并发)、QcService (缺陷处置)、IntegrationService (熔断/Saga) 单测 |

### 3.7 mes-iot Windows 构建空转

| 维度 | 详情 |
|------|------|
| **影响** | healthz 探针在 Windows 下为空循环，形同虚设 |
| **位置** | `mes-iot/src/main.cc` L38-42 (Windows 构建直接跳过 healthz) |
| **建议** | Windows 下用 TCP socket 实现或条件编译跳过 + 文档说明 |

### 3.8 IoT 模拟器掩盖问题

| 维度 | 详情 |
|------|------|
| **影响** | `scripts/iot_simulator.py` 长期替代真实采集，可能掩盖集成问题 |
| **建议** | 模拟器标注为"开发用"，生产环境必须部署真实 mes-iot |

---

## 4. 数据库索引与约束缺失

### 4.1 缺失索引

| 表 | 缺失索引 | 影响查询 | 位置 |
|----|---------|---------|------|
| `prod_work_orders` | `(created_by)` | data_scope 过滤对每条工单查询全表扫 | WorkOrderService.cc L60-87 |
| `qc_inspections` | `(inspected_at)` | 统计按日趋势 GROUP BY 时间范围扫描 | QcService.cc L423-432 |
| `sys_audit_logs` | `(module)` | 按 module 过滤审计日志 | SystemService.cc L715 |
| `mq_outbox` | `(status, retry_count)` | OutboxDispatcher 重投场景扫描 | OutboxDispatcher.cc |
| `iot_alerts` | 无分区/保留策略 | 告警表无限增长 (对比 iot_raw_data 90 天保留) | 004 迁移 |

### 4.2 缺失约束

| 表 | 缺失约束 | 风险 |
|----|---------|------|
| `iot_raw_data` | `device_id`/`sensor_id` 无外键 | 分区表性能取舍，但未注释说明 |
| `prod_work_orders` | `erp_order_no` 无 UNIQUE | 可能重复关联 ERP 订单 |
| `integ_erp_orders` | `work_order_id` 无 FK | 工单删除后孤立记录 |
| `qc_defects` | `station_id`/`operator_id` 无 FK | 工位/用户删除后孤立记录 |

---

## 5. 设计文档漂移记录

> 以下为设计文档与实际实现不一致之处，需同步更新文档或代码。

| 项目 | 设计文档 | 实际实现 | 建议 |
|------|---------|---------|------|
| WS 频道名 | `alert.active` | `alert` | 统一为 `alert`，更新设计文档 |
| 报工字段 | `operation_id/workstation_id/operator_id/shift` | `step_seq` | 扩展报工接口字段 |
| 验证码 | 图形验证码 | 明文返显 | 实现图形验证码 |
| 审计日志过滤 | `operation`/`start_time`/`end_time` | `user_id`/`module` | 补齐过滤条件 |
| 用户列表响应 | `items` | `list` | 统一响应字段名 |
| FEATURE_INVENTORY.md | "图形验证码 ✅" | 明文验证码 | 更正文档 |

---

## 6. 实施优先级矩阵

> **口径统一（2026-08-20）**：
> - **P0 阻碍生产使用**：不完成无法投产
> - **P1 提升运营效率**：运营必需，第二批
> - **P2 技术债务**：质量 / 安全 / 性能 / 可观测性 / 测试 / 索引约束 / 文档漂移
> - **P3 功能增强**：二期协议（OPC-UA / MQTT）、移动端、物料 BOM 等增值功能
>
> **P4(5.1–5.7) 关闭结果同步**：原 P0 共 6 项，除「GA 标准 2h 压测」外，其余 5 项（IoT Modbus 采集、停采链路两端、大屏 ECharts、前端 3 模块、验证码图形）已由 P4 关闭；原 P1 中「OEE 自动计算消费者」「IoT 域管理补齐」也由 P4 关闭。下表仅列**剩余待办**，已关闭项见末节追溯。

### P0 — 阻碍生产使用（必须先做）

| # | 缺口 | 状态 | 工作量 | 依赖 |
|---|------|------|--------|------|
| 6 | GA 标准环境 2h 压测验证 | 🔴 待执行 | 1-2 天 | 部署标准服务器 |

> 原 P0 #1–#5 已由 P4 关闭：IoT Modbus 采集（5.1）、停采链路两端（5.2）、大屏 ECharts（5.3）、前端 3 模块（5.5）、验证码图形（P3-4.1）。

### P1 — 提升运营效率（第二批）

| # | 缺口 | 工作量 | 依赖 |
|---|------|--------|------|
| 7 | 工单 cancel 路由 + 主数据 CRUD 补齐 | 3-5 天 | 无 |
| 8 | 用户/审计导入导出 + 配置刷新 | 2-3 天 | 无 |
| 9 | 质量追溯链修复（处置人 + 质检联动报工） | 1-2 天 | 无 |
| 11 | Alertmanager + Grafana 部署 | 2 天 | 无 |
| 12 | CI/CD Pipeline 配置 | 3-5 天 | CI 已修复 |

> 原 P1 #10（PAD 移动端）移至 **P3**；原 P1 #13（OEE 消费者）、#14（IoT 域管理补齐）已由 P4 关闭。

### P2 — 技术债务（持续改进）

| # | 缺口 | 工作量 | 依赖 |
|---|------|--------|------|
| 15 | SQL 拼接迁移到 SqlParams 占位符 | 1 周 | 无 |
| 16 | 源码注释编码修复 | 1 天 | 无 |
| 17 | 登录验证缓存安全加固（认证绕过漏洞已于 08-18 修复 17c7385，建议复核缓存策略） | 半天 | 无 |
| 18 | 自签证书私钥从仓库移除 | 半天 | 无 |
| 19 | 监控 exporter 补齐（node/postgres/redis/rabbitmq） | 2 天 | #11 |
| 20 | Service 层单测补齐 | 1-2 周 | 无 |
| 21 | 缺失索引添加 | 1 天 | 无 |
| 22 | 缺失约束添加 | 1 天 | 无 |
| 24 | 日志聚合（Loki） | 1 周 | 无 |
| 25 | 设计文档漂移修正 | 1 天 | 无 |

### P3 — 功能增强（二期 / 增值）

| # | 缺口 | 工作量 | 依赖 |
|---|------|--------|------|
| 10 | PAD 移动端（PWA） | 3-4 周 | 前端 3 模块 |
| 23 | 多看板配置 + WS 会话审计 | 1 周 | 无 |
| — | OPC-UA 采集（二期） | 2-3 周 | 无 |
| — | MQTT 网桥（二期） | 1 周 | 无 |
| — | 物料 / BOM 本地管理 | 1-2 周 | 无 |
| — | publisher confirms（MQ 确认，待 SimpleAmqpClient 升级） | 待库升级 | 无 |

### 已关闭项追溯（P4 及近期修复）

| 原编号 | 缺口 | 关闭依据 |
|--------|------|---------|
| P0 #1 | IoT Modbus TCP 采集 | P4-5.1 ✅ |
| P0 #2 | 停采指令链路两端 | P4-5.2 ✅ |
| P0 #3 | 大屏 ECharts 集成 | P4-5.3 ✅ |
| P0 #4 | 前端设备/质量/集成页面 | P4-5.5 ✅ |
| P0 #5 | 验证码图形渲染 | P3-4.1 ✅ |
| P1 #13 | OEE 自动计算消费者 | P4-5.4 ✅ |
| P1 #14 | IoT 域管理补齐 | P4-5.7 ✅ |

---

## 附录: 审计方法说明

本报告基于以下审计手段:
- **后端**: 13 个 Controller (.cc) + 8 个 Service (.cc) + 9 组迁移 (up/down SQL) 逐文件阅读
- **前端**: mes-web 8 个页面 + mes-dashboard 全部组件源码审查
- **IoT**: mes-iot/src/main.cc 全文阅读
- **部署**: docker-compose.prod.yml + nginx.conf + prometheus 配置审查
- **脚本**: 12 个脚本 (Python/PowerShell) 全部阅读
- **测试**: 5 个单元测试 + 6 个 E2E 脚本审查
- **交叉验证**: 设计文档 (MES_Architecture_Design.md) 与实现逐节对比

---

> **文档版本**: 1.1 | **最后更新**: 2026-08-20 | **维护者**: MES 团队
