# HMS 核心功能完善方案（评审稿 v1.1）

> 版本: 1.1 | 日期: 2026-08-16 | 状态: **评审通过（APPROVED WITH MINOR FIXES）**
>
> ## 评审记录（2026-08-16 双 agent 技术评审）
>
> - 总体结论：**APPROVED WITH MINOR FIXES**。17 项 P1 声明经逐行核验绝大部分与源码吻合，文件/行号引用准确。
> - 评审通过后按 P1→P5 顺序实施，每批独立提交并回归（CI 门禁 + E2E）。
>
> | 评审修正项 | 影响批次 | 处置 |
> |-----------|---------|------|
> | 2.4 无需新增冗余列：`prod_work_order_operations` 已有 `process_step_id`，直接 JOIN `prod_process_steps.quality_check` | P1 | 已修订方案 |
> | 2.9 前置缺失：`last_heartbeat_at` 全仓无写入方，须先让 DataIngestHandler 写心跳 | P1 | 已补充前置项，并拆分后移 |
> | 2.10 `else if` 修复不成立（组合过滤仍失效），须改动态 WHERE 拼接 | P1 | 已修订方案 |
> | 2.10 Saga 直改状态不写 `actual_end_at`/停采 outbox，连带破坏停采链路 | P1 | 已补充到方案+验收 |
> | P3 新增【严重】：登录/改密明文密码落入 `sys_audit_logs.request_params` | P3 | 新增 4.5 |
> | 5.1 iot.json 缺 `unit_id`；40001 地址需换算协议偏移；配置双源须定事实源 | P4 | 已补充 |
> | 5.2 拓扑缺陷：`cmd.#` 绑唯一 `iot.cmd.queue` 与后端消费竞争，须新增独立队列 | P4 | 已补充 topology.json 变更 |
> | 5.4 A 因子无数据源：须约定 run_status 传感器 + 班次归属规则 | P4 | 已补充 OEE 口径定义 |
> | 6.4 Grafana 硬缺口：缺 4 个 exporter + rabbitmq 插件 | P5 | 已补充 |
> | 6.5 hms-iot 无 CI job 无 Dockerfile | P5 | 已补充，提前到 P4 初期 |
> | 其他：大屏弱凭据、createPlan 单号碰撞、outbox 无终态、hms-web 缺图表库 | 各批次 | 已并入对应章节 |

---

## 目录

1. [优先级与总体路线](#1-优先级与总体路线)
2. [第一批：已有核心功能正确性](#2-第一批已有核心功能正确性)
3. [第二批：已有核心功能稳定性](#3-第二批已有核心功能稳定性)
4. [第三批：已有核心功能安全性](#4-第三批已有核心功能安全性)
5. [第四批：缺失的核心功能](#5-第四批缺失的核心功能)
6. [第五批：增强缺口](#6-第五批增强缺口)
7. [依赖关系与里程碑](#7-依赖关系与里程碑)
8. [风险与规避](#8-风险与规避)

---

## 1. 优先级与总体路线

按用户指定优先级排序，每批完成后回归验证再进入下一批：

| 批次 | 主题 | 内容 | 预估工作量 | 产出 |
|------|------|------|-----------|------|
| P1 | 正确性 | 17 项缺陷修复（含 2 个严重） | 4-5 天 | 全 API 行为符合设计文档 |
| P2 | 稳定性 | 并发/事务/边界加固 + 单测补齐 | 5-7 天 | 关键路径压力验证通过 |
| P3 | 安全性 | 验证码/缓存/黑名单/密钥/审计脱敏 | 2-3 天 | 渗透自检清单全过 |
| P4 | 缺失功能 | IoT 采集/停采链路/大屏图表/前端 3 模块 | 4-6 周 | 生产可用闭环 |
| P5 | 增强 | PAD/导入导出/监控/CI-CD | 3-4 周 | 运营效率提升 |

**总原则**：
- 每批独立提交、独立回归（CI 门禁 + E2E），杜绝大泥球提交。
- 数据库变更一律新增迁移（up/down 成对），不改历史迁移。
- 新增路由必须同步 `perm_routes.cc` 与权限种子（CI 权限映射门禁会拦截）。
- 拓扑变更（topology.json）与代码同步提交，dev/生产 compose 同时更新。

---

## 2. 第一批：已有核心功能正确性

> 目标：现有 76 条 API 的行为与设计文档一致，消灭"能用但错"的隐性缺陷。
> 源码审计已确认 17 项 + 评审新增 2 项（详见下文）。

### P1 实施顺序（评审建议调整后）

```
2.1 profile 挂起（孤立低风险，首批）
  → 2.10 集成过滤 + Saga 补齐（与 2.2 联动，优先）
  → 2.5 缺陷处置 userId → 2.6 cancel 路由 → 2.7 主数据 CRUD → 2.8 审计过滤
  → 2.11 小项（含 createPlan 单号碰撞） → 2.3 字段改名（与大屏 5.3 绑定发布）
  → 2.4 质检门禁（热路径，独立回归单元，灰度开关）
  → 2.2 后端停采二次投递（先落库可观测，IoT 端随 5.1 交付）
  → 2.9 拆分：心跳写入前置（随 2.2）＋离线判定；其余告警状态机/CRUD 并入 P4
```

### 2.1 【严重】`profile` 用户不存在时 HTTP 挂起

| 项 | 内容 |
|----|------|
| 位置 | `AuthService.cc` L274-276 |
| 现状 | 查询结果为空时 `return;` 既不调 `onOk` 也不调 `onErr` → 请求悬挂直至 Drogon 超时。全仓复扫确认**仅此一处**真正静默悬挂（其余 Service 均 `onOk(nullptr)` 交控制器转 404） |
| 影响 | 被删用户携带旧 token 调 `/auth/profile` 会占用连接 60s+；慢查询放大 |
| 技术方案 | 空结果改为 `onErr(404, "用户不存在")`；顺带 grep 全部 Service 的 `r.empty()` 分支做防御性清扫 |
| 实施计划 | ① 修复 profile 空分支；② 全仓扫描确认无其他静默 return；③ 增加单测（构造不存在用户） |
| 验收标准 | ① 不存在用户调 profile 返回 404 且 <50ms；② CI 全绿；③ 全仓无"静默 return 无回调"残留 |

### 2.2 【严重】停采指令链路两端空转

| 项 | 内容 |
|----|------|
| 位置 | 后端 `StopCollectionHandler.cc` L46-49（仅 LOG）；IoT `main.cc` L195（TODO） |
| 现状 | 工单报工满量 → 写 `mq_outbox` → 投递到 `iot.cmd.queue` → 消费者仅打日志 → **设备继续采集** |
| 影响 | 完工后设备仍采集，数据持续入池、产生无主数据与无效告警 |
| 技术方案 | 后端：`StopCollectionHandler` 解析消息体（work_order_id）→ 查该工单绑定设备（工单→line_id→iot_devices.line_id）→ 逐台向 `iot.exchange/cmd.stop.{device_id}` 发停采指令（复用 `MqProducer`，消息体含 device_id + 幂等键 work_order_id+device_id）；IoT：新增独立消费者（见拓扑变更），收到停采指令后暂停对应设备轮询 |
| 拓扑变更（评审修正） | topology.json **新增 `iot.cmd.collector.queue`**（binding `cmd.stop.#` + `cmd.dev.#` → hms-iot 独占消费）。原因：现 `cmd.#` 绑唯一 `iot.cmd.queue`，后端 StopCollectionHandler 与 IoT 消费者会 round-robin 竞争，消息可能被任一方独占。**实现补强（P1-2.2）**：`iot.cmd.queue` 绑定从 `cmd.#` 收紧为精确 `cmd.stop_collection`——否则后端二次投递的 `cmd.stop.{device_id}` 会匹配 `cmd.#` 回环进自身队列造成无限循环；同时 `IotService` 设备指令 routing key 由 `cmd.{device_id}` 改为 `cmd.dev.{device_id}`（并入 collector 队列），并在 handler 内加 relayed-key 防御性 ack |
| 实施计划 | ① 后端实现停采指令二次投递（先打点可观测，IoT 消费者未上线前不丢消息）；② topology.json 变更 + compose 同步；③ IoT 实现指令消费与轮询暂停/恢复（随 5.1）；④ E2E：建单→开工→报满→断言设备停止上报 |
| 验收标准 | ① 工单完工 2s 内目标设备停止上报；② 重启后设备恢复采集；③ 幂等：重复停采指令不报错；④ 消息不被错误竞争消费（独立队列验证） |

### 2.3 【高】大屏 OEE 为伪 OEE

| 项 | 内容 |
|----|------|
| 位置 | `WsBroadcastManager.cc` L114（`oee = good_qty/plan_qty*100` 实为完工率）；dashboard 降级路径 App.vue L68 也有一份 |
| 影响 | 管理层看板数据失真，误导决策 |
| 技术方案 | 阶段一（正确性）：字段改名 `yield_rate` 保留完工率语义，杜绝误读，前端两处消费同步改；阶段二（缺失功能）在 MQ 消费者中实现真 OEE（见 5.4）。**与 5.3 大屏重构绑定发布，避免双改** |
| 实施计划 | ① 改字段名并同步 dashboard 前端消费（App.vue 两处）；② 更新 contracts/ws-push.schema.json（评审提示：现契约仅信封，payload 为 `object`，需**新增各频道 payload 子 schema**，非仅改字段名） |
| 验收标准 | ① 大屏不再出现名为 oee 实为完工率的字段；② 契约文件含频道级 payload schema |

### 2.4 【高】报工不校验质量门禁

| 项 | 内容 |
|----|------|
| 位置 | `WorkOrderService.cc` report（L347-429） |
| 现状 | 工艺步骤 `quality_check=true` 时，报工/完工完全不校验该工序是否存在合格检验记录 |
| 影响 | 需质检的工序可跳过质检直接完工，质量追溯链断裂 |
| 技术方案（评审修正） | **无需新增冗余列**：`prod_work_order_operations` 已有 `process_step_id`（create 时写入），report 事务内 `JOIN prod_process_steps ps ON ps.id = op.process_step_id WHERE ps.quality_check` 判断该工序是否需质检；若需质检，校验 `qc_inspections` 存在 `result IN (1 合格, 3 让步)` 记录且关联该工单+工序；无记录则 409 拒绝并提示先质检。**灰度开关**：`sys_configs.quality_gate_enabled` 默认开启、可回退（用户已裁决接受） |
| 实施计划 | ① report 事务内加校验 SQL（JOIN process_steps + qc_inspections 存在性）；② 灰度开关读取；③ E2E：质检缺省时报工被拒、开关关闭后放行 |
| 验收标准 | ① 质检工序无合格记录时报工返回 409 且数量不变；② 有合格记录正常报工；③ 开关关闭后行为回退到现状；④ 不影响非质检工序 |

### 2.5 【高】缺陷处置不记录处置人

| 项 | 内容 |
|----|------|
| 位置 | `QcService.cc` L376 `(void)userId;` |
| 现状 | 处置人 ID 被丢弃，`qc_defects` 无处置人/处置时间字段 |
| 影响 | 质量追溯无法回答"谁决定的返工/报废"，审计不闭环 |
| 技术方案 | 迁移 010 给 `qc_defects` 加 `disposition_by BIGINT`、`disposition_at TIMESTAMPTZ`；`handleDefect` UPDATE 一并写入（现有 `disposition=0` 并发保护保留，仅待处理可处置） |
| 实施计划 | ① 迁移；② 改 UPDATE SQL + 参数；③ 响应体带处置人；④ 单测 |
| 验收标准 | ① 处置后 `disposition_by/disposition_at` 落库；② 审计日志可关联到操作者 |

### 2.6 【中】工单 cancel 路由缺失

| 项 | 内容 |
|----|------|
| 位置 | `WorkOrderController.cc`（状态机已支持 Cancel，路由未暴露） |
| 技术方案 | 加 `PUT /api/v1/production/work-orders/{id}/cancel`，复用 `transit(id,"cancel")`；同步 perm_routes + 种子 |
| 实施计划 | ① 路由+handler；② 权限映射；③ E2E：待排产/已排产/已下达可取消，进行中拒绝 |
| 验收标准 | ① 合法状态取消成功；② 非法状态 409；③ CI 权限门禁通过 |

### 2.7 【中】主数据 CRUD 不完整

| 项 | 内容 |
|----|------|
| 位置 | `ProductionController.cc` |
| 现状 | 产线/产品/工艺只有 Create+List，无 PUT/DELETE；列表无分页；`plans` 硬编码 `LIMIT 100`（L146）；`lines` 响应漏 `location` 字段（L39-49，SELECT 有 L34 JSON 无） |
| 技术方案 | ① 产线/产品/工艺各补 PUT/DELETE（软删）；② 列表加分页参数（page/page_size 默认 20）；③ plans LIMIT 改分页；④ lines 响应补 location |
| 实施计划 | ① 控制器路由；② Service 方法；③ 权限种子；④ E2E 主数据 CRUD |
| 验收标准 | ① 各主数据可编辑/软删；② 列表分页正确返回 total/page；③ 被引用数据软删时给出 409 提示（有工单引用） |

### 2.8 【中】审计日志过滤不完整

| 项 | 内容 |
|----|------|
| 位置 | `SystemService.cc` L704-715 |
| 现状 | 仅支持 user_id/module 过滤，缺 operation/start_time/end_time/response_code/ip |
| 影响 | 合规审计无法按时间范围拉取（分区表全扫） |
| 技术方案 | WHERE 补 5 个可选条件（全部参数化，动态拼接 WHERE，禁止字符串拼接 SQL）；响应保留现有字段 |
| 实施计划 | ① SQL 改造；② 前端审计页补筛选表单 |
| 验收标准 | ① 按时间段查询命中 `sys_audit_logs` 分区裁剪（EXPLAIN 验证——该表按 created_at 分区，**查询必须含 created_at 范围条件**才有裁剪效果）；② 组合条件正确 |

### 2.9 【中】IoT 域管理缺口（评审修正：拆分并后移）

> 评审发现：全仓 grep `last_heartbeat_at` **无任何写入方**（DataIngestHandler 从不更新它），离线检测前提不存在。故本项拆分：
> **A 部分（随本批）**：心跳写入（DataIngestHandler 收到上报时更新 `last_heartbeat_at`）+ 离线判定定时任务（扫 `>60s` 置离线 + 发 OFFLINE 告警）。
> **B 部分（并入 P4 IoT 管理）**：告警状态机补 `1→2 消除 / 0,1→3 忽略`、传感器 PUT/DELETE、设备类型 CRUD、listAlerts 补 `acknowledged_by` 字段（SELECT 有 L442，JSON 无）。

| 项 | 内容 |
|----|------|
| 位置 | `IotService.cc` / `IotController.cc` / `DataIngestHandler.cc` |
| A 部分方案 | ① DataIngestHandler 上报入库时 UPDATE `iot_devices.last_heartbeat_at`；② 定时任务（复用现有 scheduler）扫 `last_heartbeat_at < now()-60s AND status=1` → 置离线 + 复用 AlertHandler 发 OFFLINE 告警 |
| A 验收 | ① 设备停报 60s 后状态自动离线且生成 OFFLINE 告警；② 恢复上报后重新在线 |
| B 部分 | 并入 P4（5.7 IoT 管理补齐），验收：告警可消除/忽略；传感器可编辑删除；listAlerts 含 acknowledged_by |

### 2.10 【中】集成服务过滤 bug 与事务直改状态（评审修正）

| 项 | 内容 |
|----|------|
| 位置 | `IntegrationService.cc` |
| 现状 | ① listLogs 同时传 systemType+status 时 status 过滤失效——`L419-443` 两个独立 `if`，systemType=ERP 分支直接返回，status 被跳过；② reportCompletionSaga 绕过状态机直接 `UPDATE prod_work_orders SET status = 5`（L276-277），**且不写 `actual_end_at`、不写 stop_collection outbox**——经 Saga 完工的工单永远不会触发停采链路 |
| 技术方案（评审修正） | ① **不能只改 else-if**（ERP 分支仍会跳过 status）——改为动态 WHERE 拼接（参数化，systemType/status 均可选）；② Saga 完工改为调用 `WorkOrderService::transit` 或等价逻辑：走状态机、写 `actual_end_at`、写 stop_collection outbox |
| 实施计划 | ① listLogs 动态 WHERE；② Saga 完工改状态机路径；③ 单测：组合过滤 + Saga 状态 + outbox 落库 |
| 验收标准 | ① 组合过滤结果正确（ERP+status、WMS+status 均生效）；② 完工后 `status/actual_end_at` 与正常路径一致；③ **Saga 完工后 mq_outbox 有 stop_collection 记录**（评审新增） |

### 2.11 【低】其他小项（含评审新增）

| # | 项 | 位置 | 修复 |
|---|----|------|------|
| a | report 不写工单 actual_start_at | `WorkOrderService.cc` | 首报时补 `actual_start_at`（事务内）。注：transit Start 事件已写该字段，实际影响面极小 |
| b | createProcess 事务回滚健壮性 | `ProductionController.cc` | 错误路径统一 `trans->rollback()`（Drogon 析构自动回滚，属健壮性改进非缺陷） |
| c | queryAndPushRealtime 缺 null 检查 | `WsBroadcastManager.cc` | int 字段空值兜底 0（schema 均有 DEFAULT 0，属防御性改进） |
| d | syncErpOrders URL 未编码 | `IntegrationService.cc` L177-178 | URL 参数 encode |
| e | loadConfig 按函数名取列 | `IntegrationService.cc` L65 | 显式列别名 |
| f | 源码注释编码损坏 | `WsBroadcastManager.cc` L25-27 等 | 转 UTF-8 无 BOM |
| g | **【评审新增】createPlan 单号碰撞** | `ProductionController.cc` L304 `"PL"+time(nullptr)` | 秒级时间戳同秒建两单即唯一约束冲突 500——改为序列/前缀+序号（参考 WorkOrderService 单号方案） |
| h | 【评审新增】工序级 scrap 未记录 | `prod_work_order_operations` 无 scrap 列 | 评估：报工传 scrap_qty 时落 ops 表（迁移 010 加列，或暂记质检表），排入 P2 与 3.1 并发一起处理 |

---

## 3. 第二批：已有核心功能稳定性

> 目标：关键路径在并发/故障/极端输入下行为正确，单测覆盖补齐。

### 3.1 报工并发稳定性（已有 FOR UPDATE 防护，补压测与单测）

| 项 | 内容 |
|----|------|
| 现状 | 单 SQL CTE 原子防超报（`completed_qty + $1 <= plan_qty`）；缺并发单测与压测验证 |
| 技术方案 | ① 单测：并发 100 线程报工同一工单，断言不超报；② k6 脚本增加报工并发场景 |
| 验收标准 | ① 并发报工总数 ≤ plan_qty；② P95 延迟回归不劣化 |

### 3.2 Service 层单测补齐

| 项 | 内容 |
|----|------|
| 现状 | 仅 5 个纯逻辑单测（bcrypt/circuit_breaker/data_scope_filter/jwt/state_machine），8 个 Service 零单测 |
| 技术方案 | 优先补 3 个高价值 Service：WorkOrderService（报工/状态机/超报）、QcService（处置/统计）、IntegrationService（熔断/Saga/过滤）；用测试替身注入 DB/MQ |
| 验收标准 | 单测覆盖率关键路径 ≥ 60%，CI 全绿 |

### 3.3 Outbox 可靠性回归（评审修正：补终态）

| 项 | 内容 |
|----|------|
| 现状 | advisory lock + SKIP LOCKED + 事务已可靠；**缺陷**：retry_count 达上限后行停留 `status=2` 不再被选中，无终态/DLQ/告警（评审确认） |
| 技术方案 | ① 迁移 010 增加终态 `status=3 死信`（retry_count ≥ 5 时置死信）；② 单测：投递失败→retry_count 递增→超限置死信；③ E2E：RMQ 宕机重启后积压恢复；④ 监控告警覆盖死信（alerts.yml 已有 outbox 规则，补充死信条件） |
| 验收标准 | ① 重试语义正确；② 重启后无重复/丢失；③ 死信行可见且可查询 |

### 3.4 数据库索引补齐（迁移 010）

| 项 | 内容 |
|----|------|
| 缺失 | `prod_work_orders(created_by)`、`qc_inspections(inspected_at)`、`sys_audit_logs(module)`、`mq_outbox(status,retry_count)`、`iot_alerts` 保留策略 |
| 技术方案 | 迁移 010 加 4 个索引；iot_alerts 加归档任务（pg_partman 或定时 DELETE 保留 180 天，**需确认现网是否装 pg_partman**——partman 目前仅注册 iot_raw_data 与 sys_audit_logs） |
| 验收标准 | EXPLAIN 验证查询走索引；iot_alerts 增长受控 |

### 3.5 大屏降级链路稳定性

| 项 | 内容 |
|----|------|
| 现状 | WS 断线→REST 轮询→恢复回切已实现；缺故障演练 |
| 技术方案 | E2E：杀 WS 后端实例，断言前端 3 次失败后切轮询，恢复后回切；压测 1000 WS 连接回归 |
| 验收标准 | ① 降级/回切时间符合设计（<30s）；② 无消息丢失报警 |

---

## 4. 第三批：已有核心功能安全性

### 4.1 验证码明文返显

| 项 | 内容 |
|----|------|
| 位置 | `AuthService.cc` L340-341 |
| 现状 | `dev_captcha` 直接返回明文 code，验证码形同虚设 |
| 技术方案 | 后端渲染 SVG 验证码（4 字符+噪点干扰线，Redis 存 hash 而非明文），响应仅 `captcha_id` + `captcha_image`(data:image/svg+xml;base64)；登录时校验 Redis 值（大小写不敏感）；一次一用（GET 即 DEL） |
| 实施计划 | ① 新增 `utils/Captcha.hh`（SVG 生成，纯 header，无新依赖）；② AuthService 改造（评审提示：现有 login 校验为大小写敏感精确匹配 L165，改不敏感需同步调整；且现校验流程用递归重入 login L168，改造时**提取公共校验函数**）；③ 前端登录页加验证码输入框+图片刷新；④ 单测：生成→校验→复用被拒 |
| 验收标准 | ① 响应无明文 code；② 错码 400；③ 复用同一 captcha_id 第二次必失败；④ 前端可正常登录 |

### 4.2 登录验证缓存明文密码驻留内存

| 项 | 内容 |
|----|------|
| 位置 | `AuthService.cc` L198 `cacheKey = password+"|"+hash` |
| 现状 | 密码明文作为 key 驻留进程内存 60s（压测会放大驻留量） |
| 技术方案 | 改 key 为 `username|ip|hash`（不含明文密码）；或直接移除缓存（bcrypt cost=10 已卸载到工作线程）。推荐前者，保留压测性能收益 |
| 验收标准 | ① 内存中无明文密码；② 登录性能无回退（缓存命中率≈原方案） |

### 4.3 JWT 刷新轮换（评审修正：明确启用）

| 项 | 内容 |
|----|------|
| 现状 | logout 写黑名单；refresh 查黑名单；缺"刷新后旧 refresh 立即失效"的轮换 |
| 技术方案 | **明确启用 refresh token 轮换**（改动面小）：每次 refresh 生成新 session_id、作废旧 session；前端同步更新存储 |
| 验收标准 | ① 旧 refresh 重放被拒；② 注销后 access/refresh 均不可用 |

### 4.4 自检清单（已有项确认 + 评审新增）

- ✅ JWT fail-closed、Redis 黑名单、权限 fail-closed（未注册路由 403）
- ✅ 审计日志参数化 + 批量刷盘（不阻断业务）
- ✅ 自签证书私钥已在 `.gitignore`（`deploy/nginx/certs/` 不入库，`git ls-files` 已确认）
- ✅ 密码 bcrypt cost=10 + 工作线程卸载（不阻塞 IO 循环）
- ✅ CORS：当前同域部署 + Vite 代理，无需全局放开；如需放开仅限配置白名单
- ❌ **【评审新增】大屏弱默认凭据**：`hms-dashboard/src/composables/useChannel.ts` L31-32 回退 `admin/password` 打进前端 bundle——改为从环境变量注入，生产构建无默认凭据（并入 5.3 一并处理）

### 4.5 【严重·评审新增】登录/改密明文密码落入审计日志

| 项 | 内容 |
|----|------|
| 位置 | `CrossCutting.cc` L236-263 |
| 现状 | `auditable()` 按 `/api/v1/` 前缀+方法判定，`POST /api/v1/auth/login`（公开路由）触发审计；`recordAudit` 对非 GET 直接截取 `req->body()` 前 2KB——**明文密码落库 `sys_audit_logs.request_params`**；`changePassword` 同样泄露新旧密码 |
| 影响 | 审计库被拖库 = 密码批量泄露，比 4.1/4.2 更实际 |
| 技术方案 | ① 登录/改密路径排除出审计（白名单：`/auth/login`、`/auth/change-password` 不记录 body 或仅记录元数据）；② 或对 params 做密码字段脱敏（JSON 解析后 mask `password`/`newPassword`/`oldPassword`）——推荐 ② 通用脱敏，保留审计完整性 |
| 实施计划 | ① recordAudit 加脱敏逻辑；② 单测：login 请求审计记录无明文密码；③ 回归现有审计功能 |
| 验收标准 | ① `sys_audit_logs.request_params` 无任何密码明文；② 其余审计行为不变；③ 登录流程不受影响 |

---

## 5. 第四批：缺失的核心功能

### 5.1 IoT Modbus TCP 真实采集（最大缺口）

| 项 | 内容 |
|----|------|
| 现状 | `hms-iot/src/main.cc` L193-195 显式 TODO；仅 BatchPublisher + healthz；`config/iot.json` 已定义 Modbus 设备配置格式但无消费者；生产 compose 无 IoT 容器；**CI 无 hms-iot job、无 Dockerfile**（评审确认） |
| 用户裁决 | **Modbus 轮询最小实现**（不引 libmodbus，自实现 MBAP + FC=0x03 帧，约 300 行） |
| 技术方案 | ① **协议层**：Modbus TCP 读保持寄存器——MBAP 头（7 字节，transaction id 复用做请求配对）+ 功能码 0x03；按寄存器地址排序合并成最少帧数（单帧 ≤125 寄存器）；**4xxxx 地址换算**：`address 40001 → 协议偏移 0`；② **配置事实源二选一（评审修正）**：短期——启动时经后端 REST `/api/v1/iot/devices` 拉取设备+传感器（register_addr/scale_factor/sample_interval 已在 DB），iot.json 仅留 amqp_url/healthz 等基础设施配置，**禁止文件与 DB 双写**；`device_id/sensor_id` 必须等于 DB 主键，启动时校验拒绝启动；③ **iot.json 补 `unit_id` 字段**（评审确认缺失，Modbus slave id 必需）；④ 采集调度：轮询循环+心跳上报+断连指数退避重连；⑤ 复用 BatchPublisher 发布 `iot.exchange/data.report`，**补 publisher confirms**（main.cc L149 留白）；⑥ `cmd.#` 消费者支持停采/恢复（见 5.2）；⑦ 生产 compose 增加 `hms-iot` 容器（healthcheck 8091）；⑧ OPC-UA/MQTT 二期 |
| 实施计划 | ① 协议层（帧编解码）；② 配置加载（后端 API 拉取 + 校验）；③ 采集调度（轮询+心跳+重连）；④ cmd 消费者；⑤ 容器化+compose+CI job；⑥ 与后端 DataIngestHandler 联调；⑦ scripts/ 增补 `modbus_slave_sim.py`（pymodbus 模拟从站）E2E |
| 验收标准 | ① 模拟从站数据按周期入 `iot_raw_data`（时序正确）；② 停采指令 2s 内生效；③ 断连自动重连（指数退避）；④ 容器健康检查通过；⑤ 100 设备轮询 P95 < 500ms；⑥ CI 有 iot 编译 job 且全绿 |

### 5.2 停采链路（承接 2.2，两端落地）

> 依赖 5.1 的 cmd 消费者。后端 2.2 先行，IoT 端随 5.1 交付。
> 拓扑变更见 2.2（新增 `iot.cmd.collector.queue`）。评审建议：后端二次投递先落库可观测，等 IoT 消费者上线再验收端到端，避免中间态消息被 ack 丢弃。

### 5.3 大屏 ECharts 可视化

| 项 | 内容 |
|----|------|
| 现状 | `package.json` 声明 echarts ^5.5.1 但零 import；App.vue 用 `<pre>` 渲染 JSON |
| 技术方案 | ① 布局重构：顶部 KPI 行（在制工单/完工率/OEE/告警数）+ 中部图表网格；② ECharts 按需引入（echarts/core + Gauge/Line/Graph，tree-shaking），自建 useChart composable 封装 init/resize/dispose；③ 数据源：WS 三频道 + REST 兜底（已有降级逻辑复用）；④ 深色主题适配；⑤ **弱默认凭据移除**（见 4.4）；⑥ **评审切分**：先交付 KPI/产线状态/质量趋势/告警时间线 4 类图，OEE 仪表盘在 5.4 真 OEE 落地后接入（此前只显示 yield_rate 完工率） |
| 实施计划 | ① 组件拆分（DashboardKpi/LineStatus/QualityTrend/AlertTimeline/OeeGauge）；② WS 消息映射到图表数据（production.realtime 按 line_id 聚合——**评审提示**：1Hz 推 20 条在制工单，需定义"哪条工单代表产线"的映射规则，默认取该 line 最新一条）；③ 降级态图表走 REST；④ 截图验收 |
| 验收标准 | ① 4+1 类图表真实渲染（非 JSON dump）；② WS 推送 5s 内图表刷新；③ 降级时图表仍可用；④ 构建体积增量 < 300KB |

### 5.4 真实 OEE 计算消费者

| 项 | 内容 |
|----|------|
| 现状 | 伪 OEE（见 2.3） |
| 用户裁决 | **OEE 口径须符合 MES 规范**——采用 ISO 22400 标准定义（评审确认符合） |
| **OEE 口径定义（ISO 22400）** | ① **可用率 A** = 运行时间 / 计划生产时间。数据源：每设备约定一个 `run_status` 布尔传感器（is_key_metric=true，如寄存器 40002 运行位）；运行时长 = 心跳在线且 run_status=1 的累计时长；计划生产时长 = 按 `prod_production_plans.shift` + line_id 映射（工单无 shift 字段，**按 plan_start_at 落班**）；② **表现性 P** = 实际产出 / 理论产出 = Σ(报工量 × 理想节拍) / 运行时间；理想节拍取 `prod_process_steps.std_cycle_time`（L70）或 `prod_workstations.std_cycle_time`（L40）；③ **质量率 Q** = 合格品 / 总产出 = `qc_inspections.pass_qty / (pass_qty + defect_qty)`（005 迁移 L43-45 字段齐全）；④ **OEE = A × P × Q**，聚合粒度 `(line_id, stat_date, shift)` 写入 `prod_oee_stats`（新表） |
| 技术方案 | MQ 消费者 `oee.calc.queue`（**评审修正：topology.json 新增，与 iot.data.queue 同绑 `data.#` fan-out 复制，不影响现有入库**）：消费 `iot_raw_data`（A 因子）、报工事件（P 因子）、`qc_inspections`（Q 因子）→ 聚合写入 `prod_oee_stats`；WsBroadcastManager 改读该表推送（保留 1Hz leader 机制，只替换 queryAndPushRealtime 的 SQL 与 payload 字段：oee 拆 availability/performance/quality）；同步 contracts/ws-push.schema.json |
| 实施计划 | ① 迁移建表 `prod_oee_stats(line_id, stat_date, shift, availability, performance, quality, oee, updated_at)` + 唯一索引；② 消费者实现三因子计算（A 因子依赖 5.1 的 run_status 传感器约定）；③ WsBroadcastManager 改数据源；④ 契约更新 |
| 验收标准 | ① OEE = A×P×Q 且 0-100 合理区间；② 与手工计算一致（抽样 3 条比对）；③ 大屏显示真 OEE；④ 口径文档（A/P/Q 数据源）已评审 |

### 5.5 前端 3 大模块页面（设备/质量/集成）

| 项 | 内容 |
|----|------|
| 现状 | 后端 32 接口就绪，前端 `src/pages/` 无 iot/quality/integration 目录；路由 App.tsx L24-30 无对应条目；菜单 MainLayout 需同步 |
| 技术方案 | 复用现有 Ant Design Pro 模式（Table+Modal+Drawer+hasPerm，参照 WorkOrders.tsx）：① iot/：设备列表+详情抽屉（传感器 Tab）+告警管理（确认/消除/忽略）+采集任务 CRUD；② quality/：检验标准、检验记录（含缺陷明细）、缺陷处置（返工/返修/报废/让步）、质量统计（**评审提示：hms-web 未引 echarts/antv，需新增图表库依赖**）；③ integration/：ERP 订单同步（手动触发+日志）、WMS 领料/入库、同步日志+重试 |
| 实施计划 | 按 iot → quality → integration 顺序，每模块：**路由（App.tsx）+ 菜单（MainLayout）+ 页面 + 权限码（hasPerm，iot:* 已在 007 迁移种子）+ 请求层（复用 utils/request.ts，无独立 api 层——评审提示，勿新建层）** |
| 验收标准 | ① 32 接口全被页面覆盖；② 权限按钮级控制生效；③ 无 TS 错误、build 通过 |

### 5.6 GA 标准环境 2h 容量验证（评审修正）

| 项 | 内容 |
|----|------|
| 现状 | `perf/k6/m2_composite.js` **是 10 分钟脚本**（stages 30s+9m30s，评审确认），并非"就绪可跑 2h" |
| 技术方案 | ① 参数化 m2_composite.js 的 duration（或新写 m3_ga.js）；② 标准服务器（≥8C16G，与生产同规格）跑 2h 复合压测：WS 1000 连接 + REST 读写 + 报工并发；③ **明确环境**：prod compose（Redis Cluster/RMQ 集群行为与 dev 差异大）；④ k6 阈值按 2h 稳态重新校准（不沿用 10min 值）；⑤ **评审建议：P5.4 监控提前并行交付**，否则 2h 长跑只能靠裸指标排查内存/连接泄漏 |
| 验收标准 | P95 ≤ 500ms、错误率 < 0.1%、WS 不掉线率 > 99.9%（基线参考本机 278ms）、内存/连接无泄漏趋势 |

### 5.7 IoT 管理补齐（承接 2.9 B 部分）

> 告警状态机 `1→2 消除 / 0,1→3 忽略` 端点、传感器 PUT/DELETE、设备类型 CRUD、listAlerts 补 `acknowledged_by`。schema 已支持（004 迁移 L128 定义 2/3 状态），纯 API 层补齐。

---

## 6. 第五批：增强缺口

> 按投入产出排序，全部复用现有后端能力。

### 6.1 PAD 扫码移动端（PWA）

| 项 | 内容 |
|----|------|
| 方案 | 新建 `hms-pad/`（Vue3 + Vant，PWA）：登录→扫码（二维码规范 `HMS:WO/OP/MAT/DEV/SN:{id}`，Camera API/蓝牙枪）→报工/质检/领料/入库；100% 复用后端 API。数据侧已验证：`prod_work_orders.erp_order_no`（003 L87）、`qc_defects.station_id/operator_id`（005 L65-66）字段齐备 |
| 评审补充 | ① 复用大屏 useChannel.ts 的"专用账号自动登录"与 ws/api token 方案；② 质检操作员权限码 `qc:*` 已在 008 迁移种子，直接复用 |
| 验收标准 | ① 扫码 2s 内进入对应操作页；② 报工/质检全流程可用；③ 离线缓存基础页面 |

### 6.2 用户/审计导入导出 + 配置刷新

| 项 | 内容 |
|----|------|
| 方案 | 后端补 `POST /system/users/import`（CSV/Excel）、`GET /system/users/export`、`GET /system/audit-logs/export`（流式 CSV）、`POST /system/configs/refresh`（清 Redis 配置缓存）；前端加按钮 |
| 验收标准 | ① 导入 1000 行 < 10s 且错误行回显；② 导出列与列表一致；③ 刷新后配置即时生效 |

### 6.3 生产计划状态流转

| 项 | 内容 |
|----|------|
| 方案 | `PUT /production/plans/{id}/confirm|execute|cancel`（0→1→2 / →3，schema 003 L145 已定义 0草稿/1确认/2执行/3取消），同步权限种子 |
| 验收标准 | ① 状态机合法流转；② 非法流转 409 |

### 6.4 Alertmanager + Grafana（评审修正：补指标源）

| 项 | 内容 |
|----|------|
| 方案 | compose 增加 alertmanager（alerts.yml 已有 6 条规则，路由→邮件/钉钉 webhook）+ grafana 面板 |
| 评审硬缺口 | **prometheus.yml 目前只抓 backend:8088**——Grafana 做 node/pg/redis/rmq 面板前必须先补指标源：`node_exporter`（主机）、`postgres_exporter`（PG）、`redis_exporter`（Redis）、rabbitmq prometheus 插件（15692，需在 rabbitmq 配置启用）；否则只有 backend 指标可看 |
| 验收标准 | ① 触发一条告警可收到推送；② Grafana 面板数据可见（node/pg/redis/rmq 至少各一面板） |

### 6.5 CI/CD Pipeline（评审修正：补 hms-iot）

| 项 | 内容 |
|----|------|
| 方案 | 在已修复的 `ci.yml` 基础上：① **新增 hms-iot 编译 job**（评审确认现 CI 无 iot job、deploy/ 下无 iot Dockerfile——此缺口应提前到 P4 初期，否则 P4 的 C++ 代码无门禁）；② CD job：build 镜像→push registry→ssh 触发蓝绿部署→health check 回滚 |
| 验收标准 | ① 推送 main 自动部署到 staging；② 失败自动回滚；③ 蓝绿切换无中断；④ hms-iot 随 CI 编译 |

### 6.6 多看板 + WS 会话审计

| 项 | 内容 |
|----|------|
| 方案 | 后端支持 dashboard 配置 CRUD + WS 频道按 dashboard_id 过滤（**评审提示：WsController.cc L22 硬编码频道白名单需同步修改**）；WsController 写 `sys_websocket_sessions`（新迁移，含 dashboard_id，评审提示注明迁移编号）；前端看板切换 |
| 验收标准 | ① 多看板可切换且数据隔离；② 会话表有完整审计记录 |

---

## 7. 依赖关系与里程碑

```
P1 正确性 (4-5d)
  └─> P2 稳定性 (5-7d)          ── 依赖 P1 的修复基线
       ├─> P3 安全性 (2-3d)     ── 可并行
       └─> P4.2 停采链路后端 (随 P1，先落库可观测)
P4 缺失功能 (4-6w)
  ├─ 5.1 IoT 采集 ──> 5.2 停采两端 ──> 5.4 真 OEE（依赖采集数据）
  ├─ 5.3 大屏图表（不依赖采集，可先行；OEE 仪表盘等 5.4）
  ├─ 5.5 前端 3 模块（并行）
  └─ 5.7 IoT 管理（承接 2.9 B）
P5 增强 (3-4w)
  ├─ 6.5 hms-iot CI job 提前到 P4 初期
  ├─ 6.4 监控（Alertmanager/Grafana）提前与 5.6 GA 压测并行
  └─ 其余全部可并行，建议在 P4 中后期启动
```

**里程碑**：
- M1：P1+P2 完成 → 正确性/稳定性达标，可灰度
- M2：P3 完成 → 安全自检清单通过
- M3：P4 完成 → 生产可用闭环（采集→展示→追溯）
- M4：P5 完成 → 运营效率闭环

---

## 8. 风险与规避

| 风险 | 影响 | 规避 |
|------|------|------|
| 报工新增质检校验可能误伤既有流程 | 报工被拒 | 灰度开关：`sys_configs.quality_gate_enabled` 默认开但可回退（**用户已裁决：接受默认开启、可回退**） |
| Modbus 协议实现周期超预期 | P4 延期 | 用户已裁决轮询最小实现；模拟器先行（modbus_slave_sim.py）；先交付 5.3/5.5（无依赖） |
| 验证码改造影响登录体验 | 登录受阻 | 登录页提前联调；Redis 异常时降级为跳过验证码（记审计） |
| 大屏重构回归 | 看板不可用 | 旧版 `<pre>` 渲染保留在 feature 分支对比；截图验收 |
| 真 OEE 数据口径争议 | 指标失真 | 采用 ISO 22400 口径（A/P/Q 定义已写入 5.4），用户已裁决符合 MES 规范即可接受 |
| 迁移 010 冲突 | 升级失败 | 全部新增迁移 up/down 成对；先 dev 环境演练 |
| IoT 停采消息中间态丢失（后端先于 IoT 消费者上线） | 停采失效 | 后端二次投递先落库可观测，IoT 消费者上线后再验收端到端（评审建议） |
| 拓扑变更影响现有队列 | 消息错投 | topology.json 与 compose 同步提交；新增队列不删旧队列，灰度观察 |

---

## 附录 A：源码审计确认清单（2026-08-16，评审复核通过）

已逐行复读确认的问题（文件:行号）：
- `AuthService.cc:274-276` profile 空结果悬挂 ✅确认（全仓唯一静默悬挂）
- `AuthService.cc:340-341` 明文验证码 ✅确认
- `AuthService.cc:198` 缓存 key 含明文密码 ✅确认
- `StopCollectionHandler.cc:46-49` 停采仅日志 ✅确认
- `WsBroadcastManager.cc:114` 伪 OEE ✅确认（dashboard App.vue L68 降级路径同款）
- `WorkOrderService.cc:347-429` 无质检门禁 ✅确认（`prod_work_order_operations` 有 process_step_id 可 JOIN）
- `QcService.cc:376` (void)userId ✅确认
- `WorkOrderController.cc:14-33` 无 cancel 路由 ✅确认（状态机 Cancel 0/1/2→7 已支持）
- `ProductionController.cc:146` plans LIMIT 100 ✅确认
- `ProductionController.cc:30-55` lines 缺 location 响应字段 ✅确认
- `ProductionController.cc:304` createPlan 秒级时间戳单号 ✅确认（评审新增）
- `IotController/IotService` 告警状态机仅 0→1 ✅确认（schema 004 L128 已定义 2/3）
- `IotService.cc` listAlerts 漏 acknowledged_by（SELECT 有 L442，JSON 无）✅确认
- `DataIngestHandler` 不写 last_heartbeat_at ✅确认（评审发现，离线检测前置缺失）
- `IntegrationService.cc:419-443` listLogs 双 if 过滤失效 ✅确认（else-if 不足以修复）
- `IntegrationService.cc:276-277` Saga 直改 status=5 且不写 actual_end_at/outbox ✅确认（评审发现联动破坏）
- `IntegrationService.cc:177-178` syncErpOrders URL 未编码 ✅确认
- `CrossCutting.cc:236-263` 登录/改密明文密码入审计 ✅确认（评审新增，P3 严重）
- `deploy/nginx/certs/` 已在 .gitignore，私钥不入库 ✅（已消除）
- `CrossCutting.cc` JWT/RBAC fail-closed + 审计参数化 ✅（安全基线良好）
- `hms-dashboard/useChannel.ts:31-32` 弱默认凭据 admin/password ✅确认（评审新增）
- `hms-iot` 无 CI job、无 Dockerfile；`iot.json` 缺 unit_id ✅确认（评审新增）
- `perf/k6/m2_composite.js` 为 10 分钟脚本非 2h ✅确认（评审修正）
- `docker-compose.prod.yml` 仅 prometheus，无 alertmanager/grafana/exporter ✅确认（评审修正）
- `contracts/ws-push.schema.json` 仅信封契约，payload 为 object ✅确认（评审提示需子 schema）

---

> **评审裁决记录（用户 2026-08-16）**：
> 1. 批次划分：**合理** ✅
> 2. 质检门禁灰度开关：**接受默认开启、可回退** ✅
> 3. 真 OEE 口径：**符合 MES 规范（ISO 22400）即可接受** ✅（口径定义见 5.4）
> 4. Modbus 实现范围：**轮询最小实现** ✅
> 双 agent 评审结论：APPROVED WITH MINOR FIXES，修正项已全部并入 v1.1。**评审通过，按 P1→P5 顺序实施。**
