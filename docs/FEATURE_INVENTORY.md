# MES 已有功能清单 + 核心功能缺口分析

> 版本: 1.2 | 日期: 2026-08-20 | 基于: MES M3 定稿代码库 (P1-P3 核心完善已完成); P0/P1/P2/P3 口径已与 GAP_ANALYSIS §6 对齐

---

## 目录

1. [功能清单总览](#1-功能清单总览)
2. [后端功能清单 (76 条路由)](#2-后端功能清单-76-条路由)
3. [前端管理后台功能清单](#3-前端管理后台功能清单)
4. [大屏看板功能清单](#4-大屏看板功能清单)
5. [IoT 采集服务功能清单](#5-iot-采集服务功能清单)
6. [基础设施功能清单](#6-基础设施功能清单)
7. [核心功能缺口分析](#7-核心功能缺口分析)
8. [优先级排序与建议](#8-优先级排序与建议)

---

## 1. 功能清单总览

| 模块 | 已实现 | 部分实现 | 未实现 |
|------|--------|---------|--------|
| 用户权限 (RBAC) | ✅ 完整 | — | — |
| 生产管理 | ✅ 完整 | — | PAD 端报工 |
| IoT 设备管理 | ✅ 完整 | — | — |
| IoT 数据采集 | ✅ 完整 | — | 容器化部署待补（Dockerfile.​iot / compose 服务） |
| 质量管理 | ✅ 完整 | — | — |
| ERP/WMS 集成 | ✅ 完整 | — | — |
| 大屏看板 | ✅ 完整 | — | — |
| 可观测性 | ✅ 完整 | — | Alertmanager + Grafana |
| 高可用 | ✅ 完整 | — | GA 前标准环境 2h 压测 |
| PAD 移动端 | — | — | 全部未实现 |

---

## 2. 后端功能清单 (76 条路由)

### 2.1 认证模块 (6 接口)

| 接口 | 方法 | 路径 | 状态 | 说明 |
|------|------|------|------|------|
| 用户登录 | POST | `/api/v1/auth/login` | ✅ | 账密 + 验证码, 返回双 Token |
| 刷新令牌 | POST | `/api/v1/auth/refresh` | ✅ | refresh_token 换新 access_token |
| 退出登录 | POST | `/api/v1/auth/logout` | ✅ | JWT 加入 Redis 黑名单 |
| 当前用户 | GET | `/api/v1/auth/profile` | ✅ | 用户信息 + 权限列表 |
| 修改密码 | PUT | `/api/v1/auth/password` | ✅ | bcrypt 校验旧密码 |
| 验证码 | GET | `/api/v1/auth/captcha` | ✅ | SVG data URI 图形验证码 (P3-4.1: Redis 存 hash 非明文, 大小写不敏感, 一次一用) |

### 2.2 用户管理 (8 接口)

| 接口 | 方法 | 路径 | 状态 | 说明 |
|------|------|------|------|------|
| 用户列表 | GET | `/api/v1/system/users` | ✅ | 分页 + 按用户名/部门/状态筛选 |
| 用户详情 | GET | `/api/v1/system/users/{id}` | ✅ | — |
| 新增用户 | POST | `/api/v1/system/users` | ✅ | bcrypt 加密密码 |
| 修改用户 | PUT | `/api/v1/system/users/{id}` | ✅ | — |
| 删除用户 | DELETE | `/api/v1/system/users/{id}` | ✅ | 软删除 (deleted=true) |
| 重置密码 | PUT | `/api/v1/system/users/{id}/reset-password` | ✅ | 管理员重置 |
| 启用/禁用 | PUT | `/api/v1/system/users/{id}/status` | ✅ | 状态切换 |
| 分配角色 | PUT | `/api/v1/system/users/{id}/roles` | ✅ | 多角色分配 |

> 导入/导出接口 (`/import`, `/export`) 设计文档中有定义, 代码未实现。

### 2.3 角色权限 (8 接口)

| 接口 | 方法 | 路径 | 状态 | 说明 |
|------|------|------|------|------|
| 角色列表 | GET | `/api/v1/system/roles` | ✅ | 分页 |
| 角色详情 | GET | `/api/v1/system/roles/{id}` | ✅ | 含权限列表 |
| 新增角色 | POST | `/api/v1/system/roles` | ✅ | — |
| 修改角色 | PUT | `/api/v1/system/roles/{id}` | ✅ | — |
| 删除角色 | DELETE | `/api/v1/system/roles/{id}` | ✅ | — |
| 分配权限 | PUT | `/api/v1/system/roles/{id}/permissions` | ✅ | 批量授权 |
| 数据范围 | PUT | `/api/v1/system/roles/{id}/data-scope` | ✅ | 5 档 + 自定义部门 |
| 权限树 | GET | `/api/v1/system/permissions/tree` | ✅ | 树形结构 |

### 2.4 部门管理 (4 接口)

| 接口 | 方法 | 路径 | 状态 |
|------|------|------|------|
| 部门树 | GET | `/api/v1/system/departments/tree` | ✅ |
| 新增部门 | POST | `/api/v1/system/departments` | ✅ |
| 修改部门 | PUT | `/api/v1/system/departments/{id}` | ✅ |
| 删除部门 | DELETE | `/api/v1/system/departments/{id}` | ✅ |

### 2.5 审计日志与系统配置 (4 接口)

| 接口 | 方法 | 路径 | 状态 | 说明 |
|------|------|------|------|------|
| 审计日志查询 | GET | `/api/v1/system/audit-logs` | ✅ | 分页 + 多条件筛选 |
| 审计日志详情 | GET | `/api/v1/system/audit-logs/{id}` | ✅ | — |
| 配置列表 | GET | `/api/v1/system/configs` | ✅ | — |
| 修改配置 | PUT | `/api/v1/system/configs/{key}` | ✅ | — |

> 审计日志导出、配置刷新缓存接口设计文档中有定义, 代码未实现。

### 2.6 生产管理 (16 接口)

| 接口 | 方法 | 路径 | 状态 | 说明 |
|------|------|------|------|------|
| 工单列表 | GET | `/api/v1/production/work-orders` | ✅ | 分页 |
| 工单详情 | GET | `/api/v1/production/work-orders/{id}` | ✅ | 含工序 |
| 新增工单 | POST | `/api/v1/production/work-orders` | ✅ | 自动拆解工序 |
| 修改工单 | PUT | `/api/v1/production/work-orders/{id}` | ✅ | — |
| 排产 | PUT | `/api/v1/production/work-orders/{id}/schedule` | ✅ | 状态 0→1 |
| 下达 | PUT | `/api/v1/production/work-orders/{id}/release` | ✅ | 状态 1→2 |
| 开工 | PUT | `/api/v1/production/work-orders/{id}/start` | ✅ | 状态 2→3, 发采集指令 |
| 暂停 | PUT | `/api/v1/production/work-orders/{id}/pause` | ✅ | 状态 3→4 |
| 完工 | PUT | `/api/v1/production/work-orders/{id}/complete` | ✅ | 状态 3→5 |
| 关闭 | PUT | `/api/v1/production/work-orders/{id}/close` | ✅ | 状态 5→6 |
| 工序报工 | POST | `/api/v1/production/work-orders/{id}/report` | ✅ | 并发超报防护 (FOR UPDATE); P2-2.11h 工序级 scrap_qty 累计 |
| 产线列表 | GET | `/api/v1/production/lines` | ✅ | — |
| 新增产线 | POST | `/api/v1/production/lines` | ✅ | — |
| 工位列表 | GET | `/api/v1/production/lines/{id}/stations` | ✅ | — |
| 工艺路线 | GET | `/api/v1/production/processes` | ✅ | — |
| 新增工艺 | POST | `/api/v1/production/processes` | ✅ | — |
| 产品列表 | GET | `/api/v1/production/products` | ✅ | — |
| 新增产品 | POST | `/api/v1/production/products` | ✅ | — |
| 生产计划 | GET | `/api/v1/production/plans` | ✅ | — |
| 创建计划 | POST | `/api/v1/production/plans` | ✅ | — |

**工单状态机** (8 态):

```
0:待排产 → 1:已排产 → 2:已下达 → 3:进行中 → 5:已完工 → 6:已关闭
                                       ↕
                                      4:已暂停
                          
7:已取消 (任意状态可取消)
```

- 报工满量自动完工 (status→5), 同时写 mq_outbox 发停采指令
- 并发超报防护: `SELECT ... FOR UPDATE` + `completed_qty + report_qty <= plan_qty` 校验

### 2.7 IoT 设备域 (18 接口)

| 接口 | 方法 | 路径 | 状态 | 说明 |
|------|------|------|------|------|
| 设备列表 | GET | `/api/v1/iot/devices` | ✅ | 分页 |
| 设备详情 | GET | `/api/v1/iot/devices/{id}` | ✅ | 含传感器 |
| 新增设备 | POST | `/api/v1/iot/devices` | ✅ | — |
| 修改设备 | PUT | `/api/v1/iot/devices/{id}` | ✅ | — |
| 删除设备 | DELETE | `/api/v1/iot/devices/{id}` | ✅ | — |
| 设备状态 | GET | `/api/v1/iot/devices/{id}/status` | ✅ | 实时状态 |
| 传感器列表 | GET | `/api/v1/iot/devices/{id}/sensors` | ✅ | — |
| 新增传感器 | POST | `/api/v1/iot/devices/{id}/sensors` | ✅ | 含阈值配置 |
| 实时数据 | GET | `/api/v1/iot/devices/{id}/realtime-data` | ✅ | 从 Redis 读取最新值 |
| 历史数据 | GET | `/api/v1/iot/sensors/{id}/history` | ✅ | 分区表查询 + 聚合 |
| 告警列表 | GET | `/api/v1/iot/alerts` | ✅ | 分页 |
| 确认告警 | PUT | `/api/v1/iot/alerts/{id}/acknowledge` | ✅ | — |
| 下发指令 | POST | `/api/v1/iot/devices/{id}/command` | ✅ | 通过 MQ 下发 |
| 采集任务列表 | GET | `/api/v1/iot/tasks` | ✅ | — |
| 新增采集任务 | POST | `/api/v1/iot/tasks` | ✅ | — |
| 修改采集任务 | PUT | `/api/v1/iot/tasks/{id}` | ✅ | — |
| 删除采集任务 | DELETE | `/api/v1/iot/tasks/{id}` | ✅ | — |
| 启停采集任务 | PUT | `/api/v1/iot/tasks/{id}/toggle` | ✅ | — |
| 设备心跳写入 | 内部 | 上报刷新 `last_heartbeat_at` + 置在线 | ✅ P1-2.9A | DataIngestHandler 批次内去重一条 UPDATE |
| 离线判定 | 内部 | 心跳超时 60s 置离线 + OFFLINE 告警落库 | ✅ P1-2.9A | DeviceMonitor 10s 扫描, 多实例原子去重 |

### 2.8 质量管理 (7 接口)

| 接口 | 方法 | 路径 | 状态 | 说明 |
|------|------|------|------|------|
| 检验标准列表 | GET | `/api/v1/quality/standards` | ✅ | — |
| 创建检验记录 | POST | `/api/v1/quality/inspections` | ✅ | — |
| 检验记录列表 | GET | `/api/v1/quality/inspections` | ✅ | 分页 |
| 检验详情 | GET | `/api/v1/quality/inspections/{id}` | ✅ | 含缺陷 |
| 缺陷列表 | GET | `/api/v1/quality/defects` | ✅ | — |
| 缺陷处置 | PUT | `/api/v1/quality/defects/{id}/disposition` | ✅ | 返工/返修/报废/让步 |
| 质量统计 | GET | `/api/v1/quality/statistics` | ✅ | — |

### 2.9 ERP/WMS 集成 (7 接口)

| 接口 | 方法 | 路径 | 状态 | 说明 |
|------|------|------|------|------|
| ERP 订单同步 | POST | `/api/v1/integration/erp/sync-orders` | ✅ | 增量拉取 + 幂等 |
| 转化工单 | POST | `/api/v1/integration/erp/{id}/convert` | ✅ | ERP 订单→工单 |
| 工单回报 ERP | POST | `/api/v1/integration/erp/report` | ✅ | 完工回报 |
| WMS 领料 | POST | `/api/v1/integration/wms/pick-request` | ✅ | — |
| WMS 入库 | POST | `/api/v1/integration/wms/stock-in` | ✅ | — |
| 同步日志 | GET | `/api/v1/integration/logs` | ✅ | — |
| 重试同步 | POST | `/api/v1/integration/logs/{id}/retry` | ✅ | — |

**集成能力**:
- 熔断器 (CircuitBreaker): 成功重置计数, OPEN → HALF_OPEN → CLOSED 自动恢复
- Saga 补偿: WMS 入库失败时回滚 ERP 回报
- 幂等: 同步日志去重
- 重试: 失败自动重试, 超限标记失败

### 2.10 WebSocket (1 端点)

| 端点 | 说明 | 状态 |
|------|------|------|
| `/ws/dashboard` | WS 升级 + 频道订阅 | ✅ |

**频道**: `production.realtime` / `device.status` / `alert.active` / `workorder.event`

### 2.11 运维端点

| 端点 | 说明 | 状态 |
|------|------|------|
| `GET /healthz` | 健康检查 | ✅ |
| `GET /metrics` | Prometheus 指标 | ✅ |

---

## 3. 前端管理后台功能清单

### 3.1 已实现页面 (8 个)

| 页面 | 功能 | 完整度 |
|------|------|--------|
| 登录 | 账密登录, JWT 持久化, dev 默认 admin/password; P3-4.1 验证码 SVG data URI + 点击刷新 | ✅ 完整 |
| 工单管理 | 列表 + 新建 + 详情抽屉(工序 Tab) + 报工弹窗 + 8 态状态机流转 | ✅ 完整 |
| 生产计划 | 列表 + 新建 (日期/产线/班次/数量) | ✅ 完整 |
| 用户管理 | 列表/新增/编辑/启停/重置密码/分配角色 | ✅ 完整 |
| 角色管理 | 列表/新增/编辑/授权权限/5 档数据范围 | ✅ 完整 |
| 部门管理 | 树形展示 + 新增/编辑/删除 | ✅ 完整 |
| 权限管理 | 只读权限树展示与检索 | ✅ 完整 |
| 审计日志 | 分页只读查询, 按用户 ID/模块过滤 | ✅ 完整 |

### 3.2 前端基础设施

| 功能 | 状态 | 说明 |
|------|------|------|
| 认证状态管理 | ✅ | AuthProvider + localStorage 持久化 |
| 权限控制 | ✅ | 菜单按权限码动态过滤, 按钮级权限 (`hasPerm()`) |
| HTTP 封装 | ✅ | 统一响应信封, 401 自动跳登录 |
| 路由守卫 | ✅ | 未登录跳转登录页 |
| Vite dev proxy | ✅ | `/api` 代理到 8088 |
| 侧边菜单 | ✅ | 按权限码动态过滤 |
| 用户头像/角色标签 | ✅ | 顶栏展示 |

### 3.3 前端未实现页面

| 页面 | 设计文档定义 | 说明 |
|------|-------------|------|
| 设备管理 | ✅ (4.7 节) | 后端接口已有, 前端页面未开发 |
| 传感器管理 | ✅ (4.7 节) | 后端接口已有, 前端页面未开发 |
| 告警管理 | ✅ (4.7 节) | 后端接口已有, 前端页面未开发 |
| 采集任务管理 | ✅ (4.7 节) | 后端接口已有, 前端页面未开发 |
| 质量检验标准 | ✅ (4.8 节) | 后端接口已有, 前端页面未开发 |
| 检验记录 | ✅ (4.8 节) | 后端接口已有, 前端页面未开发 |
| 缺陷管理 | ✅ (4.8 节) | 后端接口已有, 前端页面未开发 |
| ERP/WMS 集成 | ✅ (4.10 节) | 后端接口已有, 前端页面未开发 |
| 系统配置 | ✅ (4.11 节) | 后端接口已有, 前端页面未开发 |
| 用户导入/导出 | ✅ (4.3 节) | 后端接口未实现 |

---

## 4. 大屏看板功能清单

### 4.1 已实现

| 功能 | 状态 | 说明 |
|------|------|------|
| 三频道 WS 实时推送 | ✅ | production.realtime / device.status / alert |
| WS 鉴权 | ✅ | query token 方式 |
| 自动重连 | ✅ | 指数退避, 上限 30s |
| 降级策略 | ✅ | 连续 3 次失败切 REST 10s 轮询, 恢复自动回切 |
| 连接状态指示 | ✅ | 绿/红/黄三色 |
| 告警按级别着色 | ✅ | critical 红 / warning 黄 |
| 降级横幅提示 | ✅ | 黄色提示条 |

### 4.2 部分未实现（甘特图 / 拓扑图仍待补；ECharts 与真 OEE 已随 P4-5.3/5.4 落地）

| 功能 | 说明 | 影响 |
|------|------|------|
| ECharts 图表可视化 | ✅ 已实现 (P4-5.3): useChart 封装 + KPI/产线状态/质量趋势/告警时间线/OEE 仪表盘 | — |
| OEE 看板 (真 OEE) | ✅ 已实现 (P4-5.4): prod_oee_stats + WS 推送 A/P/Q 三因子 | — |
| 产线甘特图 | 工单排产可视化 | 仍待补 |
| 设备拓扑图 | 设备状态可视化 | 仍待补 |
| 质量趋势图 | 缺陷率趋势 | — |
| 多看板切换 | sys_websocket_sessions 表有 dashboard_id 字段 | — |

---

## 5. IoT 采集服务功能清单

### 5.1 已实现 (mes-iot/)

| 功能 | 状态 | 说明 |
|------|------|------|
| healthz 探针 | ✅ | Linux socket, 8091 端口 |
| BatchPublisher 批量发布 | ✅ | 条件变量+队列, 持久化消息, 100 条/100ms 刷新 |
| AMQP 连接 + 重连 | ✅ | — |

### 5.2 部分未实现（Modbus 采集 / 停采消费已随 P4-5.1/5.2 落地；OPC-UA / MQTT / epoll 仍待补）

| 功能 | 说明 | 影响 |
|------|------|------|
| Modbus TCP 采集 | ✅ 已实现 (P4-5.1): ModbusClient + DevicePoller + ConfigLoader | — |
| OPC-UA 采集 | 设计文档定义 | 仍待补（二期） |
| MQTT 网桥 | 设计文档定义 | 仍待补（二期） |
| epoll 事件循环 | 标记为 TODO | 仍待补（Linux 下可用线程池替代） |
| 停采指令消费 (iot.cmd.collector.queue) | ✅ 已实现 (P4-5.1/5.2): CmdConsumer 暂停/恢复 DevicePoller | — |
| publisher confirms 批量确认 | vcpkg SimpleAmqpClient 无 confirm API | 由 outbox 重投保证一致性 |

> **当前替代方案**: Python 模拟器 `scripts/iot_simulator.py` 可发模拟数据, 覆盖联调需求。

---

## 6. 基础设施功能清单

### 6.1 已实现

| 功能 | 状态 | 说明 |
|------|------|------|
| 开发环境 compose | ✅ | PG 单实例 + Redis + RMQ |
| 生产环境 compose | ✅ | PG 主从 + PgBouncer + Redis Cluster 6 节点 + RMQ 3 节点 + Nginx TLS + Prometheus |
| 定制 PG 镜像 | ✅ | pg_partman 5.1 + pg_cron 1.6 源码编译 |
| 数据库分区 | ✅ | iot_raw_data 按天, sys_audit_logs 按月, pg_partman 自动维护 |
| RabbitMQ 拓扑 | ✅ | 2 exchange + 5 queue + 5 binding, 有界重试 + DLQ |
| Nginx TLS + WSS | ✅ | HTTP→301 HTTPS, WSS 长连接 3600s |
| Prometheus 指标 | ✅ | 7 个指标 + 6 条告警规则 |
| 蓝绿发布 | ✅ | split_clients 10% 灰度 + 回滚 + 自愈 |
| 双实例无状态扩容 | ✅ | WS 跨实例广播 + outbox advisory lock + leader 租约 |
| 读写分离 | ✅ | PgBouncer transaction + 只读副本 DSN |
| 看板降级 | ✅ | WS 断开自动切 REST 轮询 |
| JWT 黑名单 | ✅ | Redis 存储, 注销后失效 |
| 权限缓存 | ✅ | Redis 30min TTL |
| 审计日志 | ✅ | AOP 自动记录, 分区表 + 批量刷盘 |
| Outbox 模式 | ✅ | 事务内写 mq_outbox, 投递器 advisory lock 互斥 |
| 熔断器 | ✅ | ERP/WMS 集成, header-only, 单测覆盖 |

### 6.2 部分实现 / 待 GA

| 功能 | 状态 | 说明 |
|------|------|------|
| publisher confirms | ⚠️ 预留 | vcpkg SimpleAmqpClient 无 confirm API, 由 outbox 重投兜底 |
| GA 容量验证 | ⚠️ 待完成 | 本机校准版通过, 标准环境 2h 压测待执行 |
| backend Docker 镜像 CI | ⚠️ 待完成 | 当前仅本机 exe + 手动 docker build |

### 6.3 未实现

| 功能 | 说明 |
|------|------|
| Alertmanager | 告警规则已定义, 但无告警推送通道 |
| Grafana 看板 | Prometheus 指标可查, 但无可视化看板 |
| 日志聚合 (ELK/Loki) | 当前仅容器日志 + 文件日志 |
| CI/CD Pipeline | 门禁脚本已就绪, 但无自动化 CI 配置 (GitHub Actions / GitLab CI) |

---

## 7. 核心功能缺口分析

> **口径统一（2026-08-20，与 GAP_ANALYSIS §6 对齐）**：
> - **P0 阻碍生产使用** / **P1 提升运营效率** / **P2 技术债务（质量/安全/性能/可观测性/测试/索引约束）** / **P3 功能增强（二期协议/移动端/物料 BOM 等）**
>
> **P4(5.1–5.7) 同步**：IoT 真实采集、大屏 ECharts、前端 3 模块、OEE 自动计算消费者、IoT 域管理补齐均已由 P4 关闭，下表仅列剩余待办。

### 7.1 P0 — 阻碍生产使用

| 缺口 | 影响 | 工作量 | 建议 |
|------|------|--------|------|
| **GA 容量验证** | 标准环境 2h 压测未执行, 无法确认生产容量达标 | 小 (1-2 天) | 在标准服务器上执行 `perf/k6/m2_composite.js` 加长至 2h |

> 原 P0 的 IoT 真实采集（P4-5.1）、大屏 ECharts（P4-5.3）、前端 3 模块页面（P4-5.5）已由 P4 关闭。

### 7.2 P1 — 提升运营效率

| 缺口 | 影响 | 工作量 | 建议 |
|------|------|--------|------|
| **工单 cancel 路由 + 主数据 CRUD 补齐** | 状态机支持 Cancel 但无路由; 产线/产品/工艺/计划缺 PUT/DELETE 与分页 | 中 (3-5 天) | 补 `cancel` 路由; 补齐 PUT/DELETE + 计划状态流转 |
| **用户/审计导入导出 + 配置刷新** | 批量用户手工创建; 合规审计无法导出; 配置更新不刷缓存 | 小 (2-3 天) | 后端补 `/import` `/export` + `/configs/refresh` |
| **质量追溯链修复** | 缺陷处置不记处置人; 报工不校验质检 | 小 (1-2 天) | QcService 落库 userId; 报工校验 quality_check |
| **Alertmanager + Grafana 部署** | 告警无推送通道; 指标无可视化 | 小 (2 天) | 部署 Alertmanager + Grafana 看板 |
| **CI/CD Pipeline 配置** | 无自动化部署流水线 | 中 (3-5 天) | GitHub Actions CD job: build→push→deploy→health |

> 原 P1 的 PAD 移动端移至 **P3**；OEE 消费者（P4-5.4）、IoT 域管理补齐（P4-5.7）已由 P4 关闭。

### 7.3 P2 — 技术债务

| 缺口 | 影响 | 工作量 | 建议 |
|------|------|--------|------|
| **SQL 拼接迁移 SqlParams** | 数值参数字符串拼接构造 SQL, 脆弱 | 中 (1 周) | 迁移到 `$1,$2,...` 占位符 |
| **源码注释编码修复** | 中文注释显示 `??????` | 小 (1 天) | 统一 UTF-8 无 BOM |
| **登录验证缓存安全加固** | 认证绕过漏洞已于 08-18 修复 (`17c7385`), 建议复核缓存策略 | 小 (半天) | 评估移除/收紧登录校验缓存 |
| **自签证书私钥移除仓库** | `mes.key` 私钥入库 | 小 (半天) | 移出仓库 + `.gitignore` + 部署脚本生成 |
| **监控 exporter 补齐** | 仅 backend 指标, 主机/中间件无监控 | 小 (2 天) | 加 node/postgres/redis/rabbitmq exporter |
| **Service 层单测补齐** | 8 个 Service 零单测 | 中 (1-2 周) | 优先 WorkOrderService/QcService/IntegrationService |
| **缺失索引添加** | 多表全表扫 | 小 (1 天) | 补 (created_by)/(inspected_at)/(module) 等索引 |
| **缺失约束添加** | 外键/UNIQUE 缺失致孤立/重复 | 小 (1 天) | 补 erp_order_no UNIQUE / 关键 FK |
| **日志聚合 (Loki)** | 仅容器/文件日志, 分布式查询不便 | 中 (1 周) | 部署 Loki + 采集 |
| **设计文档漂移修正** | WS 频道/报工字段/验证码/审计过滤等不一致 | 小 (1 天) | 逐条同步设计文档或代码 |

### 7.4 P3 — 功能增强

| 缺口 | 影响 | 工作量 | 建议 |
|------|------|--------|------|
| **PAD 移动端 (PWA)** | 操作员无法在工位扫码报工/质检 | 大 (3-4 周) | 独立移动端, 复用后端 API |
| **多看板配置 + WS 会话审计** | 仅一个默认看板; WS 会话无审计 | 中 (1 周) | 多看板路由 + WsController 写会话表 |
| **OPC-UA 采集** | 仅 Modbus 无法覆盖高端设备 | 大 (2-3 周) | 二期实现 |
| **MQTT 网桥** | 无法对接 MQTT 设备 | 中 (1 周) | 二期实现 |
| **物料/BOM 本地管理** | 当前依赖 ERP, 无法本地维护 | 中 (1-2 周) | 按需实现 |
| **publisher confirms** | MQ 投递靠 outbox 重投兜底, 非 confirm 模式 | 小 (待库升级) | 等 SimpleAmqpClient 升级后启用 |

### 已关闭项追溯（P4 及近期修复）

| 缺口 | 原层级 | 关闭依据 |
|------|--------|---------|
| IoT 真实采集 (Modbus) | P0 | P4-5.1 ✅ |
| 大屏 ECharts 可视化 | P0 | P4-5.3 ✅ |
| 前端设备/质量/集成页面 | P0 | P4-5.5 ✅ |
| OEE 自动计算消费者 | P2(旧) | P4-5.4 ✅ |
| IoT 域管理补齐 | P1(GAP) | P4-5.7 ✅ |

---

## 8. 优先级排序与建议

### 8.1 推荐实施路线 (按优先级)

```
第一批 (P0, 阻碍上线):
  1. GA 标准环境 2h 容量验证 → perf/k6/m2_composite.js
  （IoT Modbus 采集 / 停采链路 / 大屏 ECharts / 前端 3 模块 / 验证码图形 已由 P4 关闭）

第二批 (P1, 运营效率):
  2. 工单 cancel 路由 + 主数据 CRUD 补齐
  3. 用户/审计导入导出 + 配置刷新
  4. 质量追溯链修复（处置人 + 质检联动）
  5. Alertmanager + Grafana 部署
  6. CI/CD Pipeline 配置

第三批 (P2, 技术债务):
  7. SQL 拼接迁移 / 注释编码修复 / 登录缓存安全加固
  8. 监控 exporter 补齐 + 日志聚合 (Loki)
  9. Service 层单测补齐
  10. 缺失索引/约束 + 设计文档漂移修正

第四批 (P3, 功能增强):
  11. PAD 移动端 (PWA)
  12. 多看板配置 + WS 会话审计
  13. OPC-UA / MQTT 采集
  14. 物料/BOM 本地管理
  15. publisher confirms
```

### 8.2 架构健康度评估

| 维度 | 评分 | 说明 |
|------|------|------|
| 后端 API 完整度 | ⭐⭐⭐⭐⭐ | 76 条路由全部实现, 覆盖 6 大模块 |
| 后端工程质量 | ⭐⭐⭐⭐⭐ | 协程/事务/Outbox/熔断/分区/审计/可观测, 工业级 |
| 前端管理后台 | ⭐⭐⭐⭐ | 含设备/质量/集成 3 大模块页面 (P4-5.5) |
| 大屏看板 | ⭐⭐⭐⭐ | ECharts 图表 + 真 OEE 仪表盘已落地 (P4-5.3/5.4) |
| IoT 采集 | ⭐⭐⭐⭐ | Modbus TCP 真实采集已实现 (P4-5.1), 容器化待补 |
| 基础设施 | ⭐⭐⭐⭐⭐ | 生产级 compose + 监控 + 蓝绿 + 高可用验证 |
| 测试覆盖 | ⭐⭐⭐⭐⭐ | 54 单测全绿 (状态机/数据范围/JWT/bcrypt/熔断/Outbox/报工规则/质检规则/集成规则/审计脱敏/验证码) + E2E + 压测 + 权限门禁 |
| 可观测性 | ⭐⭐⭐⭐ | Prometheus 指标+告警规则完整, 缺 Alertmanager+Grafana |
| 文档 | ⭐⭐⭐⭐ | 架构设计+交接+构建进度+贡献指南完整, 缺部署/开发手册 (本次补齐) |

### 8.3 总结

MES 后端工程已达**工业级 MES 水准**: 76 条 API 全部实现并经过 M1-M3 全链路验证 (k6 P95=278ms, 1000 WS 连接, 蓝绿发布, 双实例扩容)。数据库设计完善 (31 表 6 模块, 分区+审计+RBAC), MQ 消息可靠 (Outbox+有界重试+DLQ), 可观测性就绪 (7 指标+6 告警)。

**剩余短板**（P4 已关闭核心功能缺口，以下为仍待办项，优先级见 §7）:
- IoT 采集：真实协议采集已实现（P4-5.1），但 mes-iot 容器化（Dockerfile.iot / compose 服务）尚未补充（P3），目前开发联调靠本地构建 + 模拟器
- PAD 移动端完全缺失（P3 功能增强，操作员无法在工位扫码作业）
- 监控可视化缺 Alertmanager + Grafana（P1 运营效率）
- 导入导出 / 计划状态流转等运营增强未实现（P1 运营效率）

后端基础设施和工程质量是项目的核心竞争力, 前端和采集端的补齐将释放已有后端能力的全部价值。

---

> **文档版本**: 1.2 | **最后更新**: 2026-08-20 | **维护者**: MES 团队
