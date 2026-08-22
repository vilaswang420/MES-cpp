# P7 主数据 CRUD 补齐 — 实现规格

> 对应 `GAP_ANALYSIS.md` §6 P1 #7「工单 cancel 路由 + 主数据 CRUD 补齐」。
> 版本: 1.1 | 日期: 2026-08-20 | 作者: MES 团队
> 修订: v1.0 → v1.1（经子代理评审 + 代码核查，修正守卫列名、部分唯一索引、create 预检、listPlans 双过滤、deleteLine 同步、权限种子幂等，见 §10）

## 0. 范围界定（核查结论）

- **工单 cancel 路由：已完整实现，不在本规格内。**
  状态机 `WorkOrderStateMachine.hh`（`kCancelled=7` + `Cancel` 事件，转移 `待排产/已排产/已下达→已取消`）、`WorkOrderController::cancel` → `transitImpl`、权限 `prod:wo:cancel` 均已落地。仅需一次独立冒烟确认（见 §8），不占工作量。
- **真实缺口：主数据 CRUD**（controller + service 两层都缺）：
  - 工位 `prod_workstations`：仅有 `GET /lines/{id}/stations`（list），缺 create / update / delete。
  - 计划 `prod_production_plans`：仅有 `GET /plans` + `POST /plans`，缺 update / delete。
  - 其余主数据（products / lines / processes / departments / roles / users）**已全 CRUD**，本规格直接复用其既有模式。
- **前端页面**：不在本规格（后端 API 范围）。前端工位/计划 CRUD 页面列为后续独立任务。

## 1. 删除策略：软删除（已与用户确认）

- 与 `lines`/`products`/`processes` **完全一致**的既有惯例：`UPDATE ... SET deleted = TRUE`，列表 `WHERE deleted = FALSE`。
- **与 #22 外键的衔接（关键，已修正）**：真正引用工位的列是 `prod_work_order_operations.workstation_id`（003:118）和 `iot_devices.workstation_id`（004:21，该表有 `deleted` 列），二者均为建表时内联 `REFERENCES` 的**默认 NO ACTION**（**不是 #22 所加**）；`qc_defects.station_id` 才是由 #22（018 迁移）加的 `ON DELETE SET NULL`。
  → 采用软删除后，**删除端点永不物理删行**，无论 NO ACTION 还是 SET NULL 的 `ON DELETE` 触发器都不会被触发，**无 FK 冲突风险**。这正是选软删除的原因（论证成立，但列名与 FK 来源须写对）。
- **真正的风险 — 物理 UNIQUE 约束（v1.0 遗漏，由 019 解决）**：`UNIQUE(line_id, station_code)`（003:45）与 `plan_no UNIQUE`（003:140）是**全表唯一**，包含软删行。若不处理，软删 ST01 后将**永远无法重建同编码工位**（且 §3 守卫返回 409 会让用户困惑）。→ 019 把这两处降级为**部分唯一索引 `WHERE deleted = FALSE`**。
- 计划删除：`prod_plan_work_orders.plan_id` 是 `ON DELETE CASCADE`，但软删不触达物理行，关联工单不被级联影响；仍用引用守卫拦截「已关联工单的计划」软删（见 §3）。

## 2. 端点清单

| 方法 | 路径 | Handler | 权限键 | 说明 |
|------|------|---------|--------|------|
| POST | `/api/v1/production/lines/{id}/stations` | `createStation` | `prod:station:add` | line_id 取路径；`(line_id, station_code)` 部分唯一冲突返回 409（见 §3 预检） |
| PUT | `/api/v1/production/stations/{id}` | `updateStation` | `prod:station:edit` | 改 station_name/station_seq/std_cycle_time/status/device_id |
| DELETE | `/api/v1/production/stations/{id}` | `deleteStation` | `prod:station:del` | 软删除 + 引用守卫（修正列名，见 §3） |
| PUT | `/api/v1/production/plans/{id}` | `updatePlan` | `prod:plan:edit` | 改 plan_date/line_id/shift/plan_qty/status；`plan_no` 不可改 |
| DELETE | `/api/v1/production/plans/{id}` | `deletePlan` | `prod:plan:del` | 软删除 + 引用守卫 |

> 既有已注册权限（无需新增）：`prod:station:list`、`prod:plan:list`、`prod:plan:add`。

## 3. Service 方法（ProductionService）

新增 5 个方法，签名对齐既有风格（`drogon::Task<nlohmann::json>`，全部 `$N` + `SqlArg` 参数化）：

- `createStation(int64_t lineId, const nlohmann::json& body, int64_t createdBy)`
- `updateStation(int64_t id, const nlohmann::json& body)`
- `deleteStation(int64_t id)`
- `updatePlan(int64_t id, const nlohmann::json& body)`
- `deletePlan(int64_t id)`

### 3.1 create 唯一预检（v1.0 遗漏）

既有 `createLine`（ProductionService.cc:146-158）**无唯一预检**，重复编码直接触发 PG 唯一违例（→ 500，非 409）。故 create 端点须显式预检：

- `createStation`：插入前
  `SELECT EXISTS(SELECT 1 FROM prod_workstations WHERE line_id=$1 AND station_code=$2 AND deleted=FALSE)`
  → 冲突 `throw Conflict("工位编码在该产线已存在")`；同时 `catch` 插入时的唯一违例兜底转 409。
- `createPlan`：`plan_no` 由服务端 `prod_plan_no_seq` 生成（`012` 已建序列），不取自用户输入，天然唯一；但仍 `catch` 唯一违例兜底转 409。

### 3.2 软删除与引用守卫（修正列名）

- `deleteStation` 守卫（**v1.0 的 `prod_work_orders.workstation_id`/`iot_raw_data.workstation_id` 是错列名，已更正**；`qc_defects` 因 #22 已 SET NULL 故移出守卫，避免有质检历史的工位永久不可删）：
  ```sql
  SELECT EXISTS(SELECT 1 FROM prod_work_order_operations WHERE workstation_id = $1)
      OR EXISTS(SELECT 1 FROM iot_devices WHERE workstation_id = $1 AND deleted = FALSE)
  ```
  有引用则 `throw Conflict("工位已被工单工序/设备引用")`；否则
  `UPDATE prod_workstations SET deleted=TRUE, updated_at=NOW() WHERE id=$1 AND deleted=FALSE RETURNING id`。
- `deletePlan` 守卫（保留）：
  `SELECT EXISTS(SELECT 1 FROM prod_plan_work_orders WHERE plan_id=$1)`
  → 有已关联工单则 `throw Conflict("计划已关联工单")`；否则
  `UPDATE prod_production_plans SET deleted=TRUE, updated_at=NOW() WHERE id=$1 AND deleted=FALSE RETURNING id`。
- 两方法均对空结果 `throw NotFound`。

### 3.3 list 过滤修正（双处）

- `listStations` 已 `WHERE line_id=$1 AND deleted=FALSE`（✅ 无需改）。
- `listPlans` 当前 `COUNT(*)`（ProductionService.cc:408）与 `SELECT`（:412-413）**两处都未过滤 deleted**（因表无该列）→ 019 加列后，**两处都须补 `WHERE pl.deleted = FALSE`**（仅改 SELECT 会导致分页 `total` 与实际不符）。

### 3.4 deleteLine 守卫同步（v1.0 遗漏）

既有 `deleteLine`（ProductionService.cc:182-186）守卫中 `prod_production_plans WHERE line_id=$1` **未过滤 deleted**；019 给计划加 `deleted` 列后，软删的计划仍会让产线永久不可删。须改为：
`EXISTS(SELECT 1 FROM prod_production_plans WHERE line_id=$1 AND deleted=FALSE)`。

## 4. 数据库迁移（019，幂等）

文件：`mes-backend/migrations/019_plan_soft_delete.up.sql`

```sql
-- 1) 计划表补软删除标志（与 prod_workstations/lines/products/processes 一致，DEFAULT FALSE 不加 NOT NULL）
ALTER TABLE prod_production_plans ADD COLUMN IF NOT EXISTS deleted BOOLEAN DEFAULT FALSE;
CREATE INDEX IF NOT EXISTS idx_plan_deleted ON prod_production_plans(deleted);

-- 2) 降级全表唯一为「部分唯一（仅未删除）」，避免软删后无法复用编码（v1.0 遗漏项）
ALTER TABLE prod_production_plans
    DROP CONSTRAINT IF EXISTS prod_production_plans_plan_no_key;
CREATE UNIQUE INDEX IF NOT EXISTS uq_plan_no
    ON prod_production_plans(plan_no) WHERE deleted = FALSE;

ALTER TABLE prod_workstations
    DROP CONSTRAINT IF EXISTS prod_workstations_line_id_station_code_key;
CREATE UNIQUE INDEX IF NOT EXISTS uq_ws_line_code
    ON prod_workstations(line_id, station_code) WHERE deleted = FALSE;

-- 3) 权限种子：工位/计划 新增 CRUD 键（仿 012 惯例，ON CONFLICT DO NOTHING 供增量补齐）
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, method, status) VALUES
('prod:station:add',  '新增工位', 3, '/api/v1/production/lines/{id}/stations', 'POST',    1),
('prod:station:edit', '编辑工位', 3, '/api/v1/production/stations/{id}',        'PUT',     1),
('prod:station:del',  '删除工位', 3, '/api/v1/production/stations/{id}',        'DELETE', 1),
('prod:plan:edit',    '编辑计划', 3, '/api/v1/production/plans/{id}',           'PUT',     1),
('prod:plan:del',     '删除计划', 3, '/api/v1/production/plans/{id}',           'DELETE', 1)
ON CONFLICT DO NOTHING;

INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'prod_manager' AND p.perm_code IN
       ('prod:station:add','prod:station:edit','prod:station:del','prod:plan:edit','prod:plan:del')
ON CONFLICT DO NOTHING;
```

`019_plan_soft_delete.down.sql`（还原 003 原始状态；权限种子不回滚，与 012 一致）：

```sql
DROP INDEX IF EXISTS uq_plan_no;
DROP INDEX IF EXISTS uq_ws_line_code;
ALTER TABLE prod_production_plans DROP COLUMN IF EXISTS deleted;
-- 还原 003 原始全表唯一（开发期 down 通常在无软删重复行时执行；若已存在软删同编码行需先清理）
ALTER TABLE prod_production_plans
    ADD CONSTRAINT prod_production_plans_plan_no_key UNIQUE (plan_no);
ALTER TABLE prod_workstations
    ADD CONSTRAINT prod_workstations_line_id_station_code_key UNIQUE (line_id, station_code);
```

> 幂等说明：所有 `ADD/DROP` 均带 `IF EXISTS/NOT EXISTS`；up 对「降级唯一」用先 DROP 原约束再 CREATE 部分索引，可重复执行。

## 5. 权限登记

- `perm_routes.cc` 新增 5 行（`add(path, method, key)`）：
  `prod:station:add` / `prod:station:edit` / `prod:station:del` / `prod:plan:edit` / `prod:plan:del`。
- **RBAC 权限树种子：写入 019 迁移**（见 §4 第 3 步，仿 `012_prod_crud_perms.up.sql` 的 `INSERT ... ON CONFLICT DO NOTHING` + `prod_manager` 绑定模式）。新环境经 `migrate up` 自动补齐；已部署环境因 `ON CONFLICT DO NOTHING` 增量补齐，不会重复。**不再"实现时定位"**——种子归属「生产管理」模块，角色分配不到则接口 403。

## 6. 实现要点（一致性清单）

1. 全部 SQL 用 `$N` + `SqlArg` 参数化（沿用 #15 审计结论，禁止字符串拼接用户输入）。
2. 数值字段解析用安全解析 + `NULLIF`（沿用 `updateUser` 修复经验，防字符串强转 500）。
3. 路由注册用 `ADD_METHOD_TO`（Drogon 宏），handler 协程 `co_await` service。
4. 响应体结构与 `createLine`/`createProduct` 对齐（返回 `{id, ...}` + 标准错误封装）。
5. 工位 `station_seq` 为顺序，update 时不做跨线唯一性重建，仅存值。
6. `createStation`/`createPlan` 必须含唯一预检 + 唯一违例兜底 409（见 §3.1）。

## 7. 工作量估算

约 **1–2 天**（远小于 GAP 标的 3–5 天）：
- 迁移 019 + 权限种子 + listPlans/deleteLine 修正：0.5h
- station 3 端点 + plan 2 端点（service+controller+perm）：0.5–1d
- RBAC 种子登记 + 构建冒烟：0.5d

## 8. 验证

1. **构建**：Windows MSVC DevShell + Ninja + Release 增量构建（同 #15 验证方式），`BUILD_EXIT=0`。
2. **迁移**：Docker PG16 跑 `migrate up`（含 019）→ 校验 `prod_production_plans.deleted` 列存在、`uq_plan_no`/`uq_ws_line_code` 部分唯一索引建立；`down -all` 往返重建。
3. **后端冒烟**（开发库或测试库）：
   - 工位：创建→列出→更新→（被 `prod_work_order_operations`/`iot_devices` 引用时）删除返回 409；**软删后同 `(line_id, station_code)` 可重建**（验证部分唯一索引生效）。
   - 计划：创建→列出（`total` 与实际条数一致，验证 COUNT 过滤）→更新→（关联工单时）删除返回 409；**软删后同 `plan_no` 可重建**。
   - 产线删除回归：软删其下某计划后，删产线不再被该软删计划拦截（验证 §3.4 同步修复）。
   - 工单 cancel：独立冒烟确认 `PUT /work-orders/{id}/cancel` 在待排产态成功、进行中态返回 409。

## 9. 不在本规格范围

- 工单 cancel 路由实现（已存在，仅冒烟）。
- 前端 mes-web 工位/计划 CRUD 页面（独立前端任务，P5）。
- PAD / OPC-UA / MQTT / 多看板等 P3 功能增强。

## 10. 修订记录

- **v1.0 → v1.1（2026-08-20，子代理评审 + 代码核查后修订）**：
  1. §1 删除策略衔接：更正错列名（`prod_work_orders.workstation_id`/`iot_raw_data.workstation_id` 不存在）→ 真实列 `prod_work_order_operations.workstation_id`、`iot_devices.workstation_id`；明确 #22 仅动 `qc_defects.station_id`；**新增物理 UNIQUE 约束风险说明**（由 019 部分唯一索引解决）。
  2. §3 守卫 SQL 更正列名；移除 `qc_defects` 守卫（SET NULL 已安全）；**新增 create 唯一预检**（v1.0 误信"照搬得 409"，实则 createLine 无预检会 500）；**listPlans 的 COUNT 与 SELECT 双处过滤**；**deleteLine 守卫同步过滤 deleted**。
  3. §4 迁移 019 重写：`ADD COLUMN` 改 `DEFAULT FALSE`（去 NOT NULL，与 003/012 惯例一致）、加 `IF EXISTS/NOT EXISTS` 幂等、降级全表唯一为部分唯一索引、权限种子写入迁移（仿 012 双写）。
  4. §5 权限登记明确写入 019 迁移，去掉"实现时定位"含糊表述。
  5. §8 验证项补：软删后同编码重建、listPlans 分页总数、deleteLine 回归。
