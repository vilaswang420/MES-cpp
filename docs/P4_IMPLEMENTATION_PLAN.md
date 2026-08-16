# MES P4 缺失功能实施方案（评审稿 v1.0）

> 版本: 1.0 | 日期: 2026-08-16 | 状态: **评审通过（APPROVED），开始实施**
>
> 前置: P1 正确性(17 项) + P2 稳定性(5 项) + P3 安全性(4 项) 已全部完成。
> 本方案覆盖 CORE_PLAN.md 第 5 章 P4 全部 7 项，按依赖关系拆分为 3 个 Sprint。
>
> ## 评审裁决（2026-08-16）
> 1. 实施顺序 S1→S2→S3：**同意** ✅
> 2. 图表库 echarts 按需引入：**同意** ✅
> 3. OEE A 因子 run_status 传感器约定 + 缺失降级 A=100%：**同意** ✅
> 4. iot.json 配置事实源 DB 拉取：**同意** ✅
> 5. 设备类型 CRUD 暂不需要：**同意** ✅

---

## 总览

| Sprint | 内容 | 依赖 | 预估 |
|--------|------|------|------|
| S1（并行启动） | 5.3 大屏 ECharts + 5.5 前端 3 模块 + 5.1 IoT Modbus 采集 + mes-iot CI | 无互相依赖 | 2-3 周 |
| S2（依赖 S1） | 5.2 停采两端 + 5.7 IoT 管理补齐 + 5.4 真 OEE 消费者 | 5.1 cmd 消费者 | 1-2 周 |
| S3（最终验证） | 5.6 GA 2h 压测 | S1+S2 全部完成 | 1-2 天 |

```
S1 并行:
  5.3 大屏 ECharts ──────────────────────────────┐
  5.5 前端 3 模块 (iot/quality/integration) ────┤
  5.1 IoT Modbus TCP 采集 ──┐                    │
  mes-iot CI/Dockerfile ────┤                    │
                            ↓                    ↓
S2 串行依赖:                 │                    │
  5.2 停采链路两端落地 ←─────┘                    │
  5.7 IoT 管理补齐 ←──────────────────────────────┘
  5.4 真 OEE 消费者 ←──── 5.1 采集数据
                            │
                            ↓
S3:  5.6 GA 标准环境 2h 压测
```

---

## Sprint 1: 并行启动（无互相依赖）

### 5.3 大屏 ECharts 可视化

> echarts ^5.5.1 已在 mes-dashboard/package.json 中声明但零 import，当前 App.vue 用 `<pre>` 渲染 JSON。

#### 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `mes-dashboard/src/composables/useChart.ts` | ECharts 封装: init/resize/dispose + tree-shaking 按需引入 |
| 新建 | `mes-dashboard/src/components/DashboardKpi.vue` | 顶部 KPI 行: 在制工单/完工率/yield_rate/告警数 |
| 新建 | `mes-dashboard/src/components/LineStatus.vue` | 产线状态网格: 各产线实时工单+进度条 |
| 新建 | `mes-dashboard/src/components/QualityTrend.vue` | 质量趋势折线图: 缺陷率/合格率时间序列 |
| 新建 | `mes-dashboard/src/components/AlertTimeline.vue` | 告警时间线: 按级别着色的垂直时间轴 |
| 新建 | `mes-dashboard/src/components/OeeGauge.vue` | OEE 仪表盘占位(显示 yield_rate，5.4 落地后替换真 OEE) |
| 修改 | `mes-dashboard/src/App.vue` | 布局重构: 替换 `<pre>` 为组件网格 + 深色主题 |
| 修改 | `mes-dashboard/src/composables/useChannel.ts` | 移除弱默认凭据(P3-4.4)，改环境变量注入 |
| 修改 | `contracts/ws-push.schema.json` | 确认 production.realtime payload 字段完整(yield_rate 已在 P1-2.3 改名) |

#### useChart.ts 设计

```typescript
// 按需引入 echarts/core (tree-shaking, 构建体积增量 < 300KB)
import * as echarts from "echarts/core";
import { GaugeChart, LineChart, BarChart } from "echarts/charts";
import { GridComponent, TooltipComponent, TitleComponent } from "echarts/components";
import { CanvasRenderer } from "echarts/renderers";

echarts.use([GaugeChart, LineChart, BarChart, GridComponent, TooltipComponent, TitleComponent, CanvasRenderer]);

export function useChart(el: Ref<HTMLElement | null>, option: Ref<EChartsOption>) {
    // init onMounted, resize on window resize, dispose onUnmounted
    // watch option -> setOption (notMerge: true)
}
```

#### App.vue 布局重构

```
┌─────────────────────────────────────────────┐
│  DashboardKpi (4 数字卡片)                   │
├──────────────────────┬──────────────────────┤
│  LineStatus          │  OeeGauge            │
│  (产线状态网格)       │  (yield_rate 仪表盘)  │
├──────────────────────┼──────────────────────┤
│  QualityTrend        │  AlertTimeline       │
│  (质量趋势折线)       │  (告警时间线)         │
└──────────────────────┴──────────────────────┘
```

#### 数据映射规则

- WS `production.realtime` → LineStatus 按 `line_id` 聚合（1Hz 推 20 条在制工单，取该 line 最新一条代表产线状态）
- WS `device.status` → LineStatus 设备状态指示灯
- WS `alert` → AlertTimeline 追加（最多保留 50 条）
- REST 降级 → 各组件切换 `degraded_source` 数据源

#### 实施步骤

1. useChart.ts composable + echarts 按需引入
2. DashboardKpi + LineStatus 组件（WS production.realtime + device.status）
3. AlertTimeline 组件（WS alert）
4. QualityTrend 组件（REST /quality/statistics 轮询 30s）
5. OeeGauge 占位（显示 yield_rate）
6. App.vue 布局整合 + 深色主题
7. useChannel.ts 弱凭据移除
8. 截图验收 + 构建体积检查

#### 验收标准

- [ ] 4+1 类图表真实渲染（非 JSON dump）
- [ ] WS 推送 5s 内图表刷新
- [ ] 降级时图表走 REST 仍可用
- [ ] 构建体积增量 < 300KB（gzip 后）
- [ ] 无弱默认凭据（admin/password 不打进 bundle）

---

### 5.5 前端 3 大模块页面（设备/质量/集成）

> 后端 32 接口就绪，前端 src/pages/ 无 iot/quality/integration 目录。复用现有 Ant Design 模式（参照 WorkOrders.tsx）。

#### 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `mes-web/src/pages/iot/Devices.tsx` | 设备列表 + 详情抽屉(传感器 Tab) + CRUD |
| 新建 | `mes-web/src/pages/iot/Sensors.tsx` | 传感器管理（嵌入设备详情 Tab，不独立路由） |
| 新建 | `mes-web/src/pages/iot/Alerts.tsx` | 告警管理: 列表 + 确认/消除/忽略 |
| 新建 | `mes-web/src/pages/iot/Tasks.tsx` | 采集任务 CRUD |
| 新建 | `mes-web/src/pages/quality/Standards.tsx` | 检验标准列表 |
| 新建 | `mes-web/src/pages/quality/Inspections.tsx` | 检验记录 + 缺陷明细 Drawer |
| 新建 | `mes-web/src/pages/quality/Defects.tsx` | 缺陷管理 + 处置弹窗(返工/返修/报废/让步) |
| 新建 | `mes-web/src/pages/quality/Statistics.tsx` | 质量统计（需图表库，见下） |
| 新建 | `mes-web/src/pages/integration/ErpSync.tsx` | ERP 订单同步(手动触发 + 日志) |
| 新建 | `mes-web/src/pages/integration/WmsOps.tsx` | WMS 领料/入库操作 |
| 新建 | `mes-web/src/pages/integration/Logs.tsx` | 同步日志 + 重试 |
| 修改 | `mes-web/src/App.tsx` | 新增 9 条路由 |
| 修改 | `mes-web/src/layouts/MainLayout.tsx` | 菜单树新增 3 个一级菜单 + 子项 |
| 修改 | `mes-web/package.json` | 新增图表库依赖（见下） |

#### 图表库选型（质量统计页）

mes-web 当前无图表库。选项：

| 方案 | 体积 | 优势 | 劣势 |
|------|------|------|------|
| @ant-design/charts | ~200KB(gzip) | 与 antd 统一设计语言 | 基于 antv/g2，较重 |
| echarts（按需） | ~150KB(gzip) | 与大屏统一技术栈 | 非 React 原生，需封装 |
| recharts | ~120KB(gzip) | React 原生，轻量 | 功能不如 echarts 丰富 |

**推荐: echarts 按需引入**（与大屏统一技术栈，封装 useChart React hook 复用大屏模式）

#### 页面范式（统一模板）

每个页面遵循 WorkOrders.tsx 模式：
1. `interface` 定义行数据 + 详情 + 分页响应
2. `useState` 管理 data/total/page/loading
3. `useCallback` + `useEffect` 加载数据
4. `hasPerm()` 按钮级权限控制
5. antd Table + Modal/Drawer + Form

#### 权限码映射

| 模块 | 权限码前缀 | 迁移种子 |
|------|-----------|---------|
| IoT | `iot:device:*`, `iot:alert:*`, `iot:task:*` | 007 迁移已播种 |
| 质量 | `qc:standard:*`, `qc:inspection:*`, `qc:defect:*` | 008 迁移已播种 |
| 集成 | `integ:erp:*`, `integ:wms:*`, `integ:log:*` | 009 迁移已播种 |

#### 实施步骤

1. 图表库引入 + useChart React hook 封装
2. IoT 模块（Devices → Alerts → Tasks，3 页面）
3. 质量模块（Standards → Inspections → Defects → Statistics，4 页面）
4. 集成模块（ErpSync → WmsOps → Logs，3 页面）
5. 路由 + 菜单 + 权限码接入
6. `npm run build` 通过 + 无 TS 错误

#### 验收标准

- [ ] 32 接口全被页面覆盖
- [ ] 权限按钮级控制生效（无权限按钮不渲染）
- [ ] 无 TS 错误、`npm run build` 通过
- [ ] 菜单按权限动态过滤

---

### 5.1 IoT Modbus TCP 真实采集（最大缺口）

> mes-iot/src/main.cc L193-196 显式 TODO。用户已裁决：轮询最小实现（不引 libmodbus，自实现 MBAP + FC=0x03，约 300 行）。

#### 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `mes-iot/src/modbus/ModbusClient.hh` | Modbus TCP 协议层: MBAP 帧编解码 + FC=0x03 读保持寄存器 |
| 新建 | `mes-iot/src/modbus/ModbusClient.cc` | socket 连接 + 请求/响应配对(transaction id) + 断连重连 |
| 新建 | `mes-iot/src/collector/DevicePoller.hh` | 单设备轮询器: 按 poll_interval_ms 调度 + 传感器地址排序合并帧 |
| 新建 | `mes-iot/src/collector/DevicePoller.cc` | 轮询循环 + scale 校验 + publisher.enqueue() |
| 新建 | `mes-iot/src/collector/ConfigLoader.hh` | 启动时经后端 REST /api/v1/iot/devices 拉取配置（禁止文件与 DB 双写） |
| 新建 | `mes-iot/src/collector/ConfigLoader.cc` | HTTP 拉取 + device_id/sensor_id 校验 + 拒绝启动 |
| 新建 | `mes-iot/src/cmd/CmdConsumer.hh` | cmd.stop.# / cmd.dev.# 消费者: 停采/恢复/设备指令 |
| 新建 | `mes-iot/src/cmd/CmdConsumer.cc` | AMQP 消费 + 解析 + 暂停/恢复 DevicePoller |
| 修改 | `mes-iot/src/main.cc` | 集成: ConfigLoader → DevicePoller 线程池 → CmdConsumer → healthz |
| 修改 | `mes-iot/config/iot.json` | 补 unit_id 字段 + 移除 devices（改 DB 拉取）+ 补 backend_url |
| 修改 | `mes-iot/CMakeLists.txt` | GLOB 自动纳入新 .cc（已用 file(GLOB_RECURSE)） |
| 修改 | `mes-iot/vcpkg.json` | 无新依赖（自实现 Modbus，不引 libmodbus） |
| 新建 | `deploy/docker/Dockerfile.iot` | IoT 容器镜像构建 |
| 修改 | `deploy/docker-compose.prod.yml` | 新增 mes-iot 容器 + healthcheck 8091 |
| 新建 | `scripts/modbus_slave_sim.py` | pymodbus 模拟从站（E2E 验证） |

#### ModbusClient 设计

```
MBAP 头 (7 字节):
  [transaction_id: 2B] [protocol_id: 2B=0] [length: 2B] [unit_id: 1B]

PDU (FC=0x03 读保持寄存器):
  请求: [function_code: 0x03] [start_addr: 2B] [quantity: 2B]
  响应: [function_code: 0x03] [byte_count: 1B] [data: N*2B]

地址换算: 40001 → 协议偏移 0 (address - 40001)
单帧限制: quantity ≤ 125 寄存器
```

#### ConfigLoader 设计

```cpp
// 启动时从后端 REST 拉取设备+传感器配置
// GET /api/v1/iot/devices?page_size=999  (需 service token)
// 校验: device_id/sensor_id 必须等于 DB 主键, 启动时校验拒绝启动
// iot.json 仅留基础设施配置: amqp_url / backend_url / healthz_port
```

#### DevicePoller 调度模型

```
每个设备一个 DevicePoller 线程:
  loop {
    1. 按传感器 address 排序, 合并连续地址成最少帧数 (单帧 ≤125 寄存器)
    2. ModbusClient.readHoldingRegisters(unit_id, start_addr, quantity)
    3. 逐传感器拆值 + scale_factor 缩放 + quality 标记 (192=Good)
    4. 组装 iot-message.schema.json 消息 → publisher.enqueue()
    5. sleep(poll_interval_ms)
  }
  // 断连: 指数退避重连 (1s → 2s → 4s → ... → 30s 上限)
```

#### CmdConsumer 设计

```cpp
// 消费 iot.cmd.collector.queue (binding: cmd.stop.# + cmd.dev.#)
// cmd.stop.{device_id}: 暂停对应 DevicePoller (atomic flag)
// cmd.dev.{device_id}:  转发设备指令 (预留, M2 可扩展)
// 消息体: { "work_order_id": 123, "device_id": 1, "action": "stop" }
// 幂等: 重复停采指令不报错 (DevicePoller 已停则跳过)
```

#### 实施步骤

1. ModbusClient 协议层（帧编解码 + socket + 重连）
2. ConfigLoader（后端 API 拉取 + 校验）
3. DevicePoller（轮询调度 + 合帧 + scale + enqueue）
4. CmdConsumer（停采/恢复指令消费）
5. main.cc 集成（ConfigLoader → Poller 线程池 → CmdConsumer → healthz）
6. Dockerfile.iot + compose 集成
7. modbus_slave_sim.py E2E 脚本
8. CI mes-iot 编译 job（提前到 P4 初期，见 6.5）

#### 验收标准

- [ ] 模拟从站数据按周期入 `iot_raw_data`（时序正确）
- [ ] 停采指令 2s 内目标设备停止上报
- [ ] 断连自动重连（指数退避，上限 30s）
- [ ] 容器健康检查通过（8091 healthz）
- [ ] 100 设备轮询 P95 < 500ms
- [ ] CI 有 mes-iot 编译 job 且全绿
- [ ] iot.json 无 devices 数组（改 DB 拉取），有 unit_id + backend_url

---

### mes-iot CI/Dockerfile（6.5 提前部分）

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `deploy/docker/Dockerfile.iot` | 多阶段构建: vcpkg → 编译 → 运行镜像 |
| 修改 | `.github/workflows/ci.yml` | 新增 iot job: cmake configure + build |
| 修改 | `deploy/docker-compose.prod.yml` | mes-iot 服务 + healthcheck + 依赖 mes-backend |

---

## Sprint 2: 依赖 S1（5.1 cmd 消费者就绪后）

### 5.2 停采链路两端落地（承接 P1-2.2）

> 后端二次投递已在 P1-2.2 实现（commit f47a6b8）。本项验收 IoT 端消费者（5.1 CmdConsumer）上线后的端到端闭环。

#### 验收内容（无新代码，E2E 验证）

1. 建单 → 开工 → 报满 → 断言目标设备停止上报
2. 重启后设备恢复采集
3. 幂等：重复停采指令不报错
4. 消息不被错误竞争消费（独立队列验证）

#### E2E 脚本

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `tests/e2e/m3_stop_collection_e2e.ps1` | 完整停采链路 E2E（需 docker + modbus_slave_sim.py） |

#### 验收标准

- [ ] 工单完工 2s 内目标设备停止上报
- [ ] 重启后设备恢复采集
- [ ] 幂等：重复停采指令不报错
- [ ] 消息不被错误竞争消费

---

### 5.7 IoT 管理补齐（承接 2.9 B 部分）

> 告警状态机 1→2 消除 / 0,1→3 忽略端点、传感器 PUT/DELETE、设备类型 CRUD、listAlerts 补 acknowledged_by。schema 已支持（004 迁移 L128 定义 2/3 状态），纯 API 层补齐。

#### 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 修改 | `mes-backend/src/services/IotService.cc` | 告警 resolve/dismiss 方法 + 传感器 update/remove + listAlerts 补 acknowledged_by |
| 修改 | `mes-backend/src/controllers/IotController.cc` | 新增路由: PUT alerts/{id}/resolve, PUT alerts/{id}/dismiss, PUT sensors/{id}, DELETE sensors/{id} |
| 修改 | `mes-backend/src/common/perm_routes.cc` | 新增 4 条权限映射 |
| 新建 | `mes-backend/migrations/016_iot_mgmt_perms.up.sql` | 权限种子: iot:alert:resolve, iot:alert:dismiss, iot:sensor:put, iot:sensor:del |
| 新建 | `mes-backend/migrations/016_iot_mgmt_perms.down.sql` | 回滚 |

#### 实施步骤

1. IotService 告警状态机方法（resolve: 1→2, dismiss: 0,1→3）
2. 传感器 PUT/DELETE（软删，校验设备引用）
3. listAlerts 响应补 acknowledged_by 字段
4. Controller 路由 + 权限映射 + 种子
5. 单测 + E2E

#### 验收标准

- [ ] 告警可消除(resolve)和忽略(dismiss)
- [ ] 传感器可编辑/删除（被引用时 409）
- [ ] listAlerts 响应含 acknowledged_by
- [ ] CI 权限门禁通过

---

### 5.4 真实 OEE 计算消费者

> 伪 OEE 已在 P1-2.3 改名 yield_rate。本项实现 ISO 22400 标准 OEE = A × P × Q。

#### 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `mes-backend/migrations/017_oee_stats.up.sql` | 建表 prod_oee_stats(line_id, stat_date, shift, availability, performance, quality, oee, updated_at) + 唯一索引 |
| 新建 | `mes-backend/migrations/017_oee_stats.down.sql` | 回滚 |
| 新建 | `mes-backend/src/services/OeeService.hh` | OEE 三因子计算 + 聚合写入 |
| 新建 | `mes-backend/src/services/OeeService.cc` | MQ 消费者: 消费 iot_raw_data(A 因子) + 报工事件(P 因子) + qc_inspections(Q 因子) |
| 修改 | `mes-backend/src/services/WsBroadcastManager.cc` | queryAndPushRealtime 改读 prod_oee_stats 推送（oee 拆 availability/performance/quality） |
| 修改 | `deploy/mq/topology.json` | 新增 oee.calc.queue (binding: data.# fan-out 复制, 不影响现有入库) |
| 修改 | `contracts/ws-push.schema.json` | production.realtime payload 新增 availability/performance/quality/oee 字段 |

#### OEE 口径（ISO 22400，用户已裁决）

```
A (可用率) = 运行时间 / 计划生产时间
  数据源: 每设备约定 run_status 布尔传感器 (is_key_metric=true, 寄存器 40002)
  运行时长 = 心跳在线且 run_status=1 的累计时长
  计划生产时长 = prod_production_plans.shift + line_id 映射 (按 plan_start_at 落班)

P (表现性) = Σ(报工量 × 理想节拍) / 运行时间
  理想节拍 = prod_process_steps.std_cycle_time 或 prod_workstations.std_cycle_time

Q (质量率) = 合格品 / 总产出
  = qc_inspections.pass_qty / (pass_qty + defect_qty)

OEE = A × P × Q
聚合粒度: (line_id, stat_date, shift) → prod_oee_stats
```

#### MQ 消费者架构

```
iot.exchange
  ├── data.# → iot.data.queue (现有, 入库)
  └── data.# → oee.calc.queue (新增, fan-out 复制)
                    ↓
              OeeService 消费
                    ↓
         聚合写入 prod_oee_stats
                    ↓
    WsBroadcastManager 1Hz 读取推送
```

#### 实施步骤

1. 迁移建表 prod_oee_stats
2. topology.json 新增 oee.calc.queue
3. OeeService 三因子计算（A 因子依赖 5.1 的 run_status 传感器约定）
4. WsBroadcastManager 改数据源（queryAndPushRealtime SQL + payload）
5. 契约更新
6. 单测：OEE 计算正确性 + 与手工计算比对（抽样 3 条）

#### 验收标准

- [ ] OEE = A×P×Q 且 0-100 合理区间
- [ ] 与手工计算一致（抽样 3 条比对）
- [ ] 大屏显示真 OEE（availability/performance/quality 拆分）
- [ ] oee.calc.queue 不影响现有 iot.data.queue 入库
- [ ] 口径文档（A/P/Q 数据源）已评审

---

## Sprint 3: 最终验证

### 5.6 GA 标准环境 2h 容量验证

> perf/k6/m2_composite.js 是 10 分钟脚本（评审确认），需参数化或新写 2h 版本。

#### 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `perf/k6/m3_ga.js` | 2h 复合压测: WS 1000 连接 + REST 读写 + 报工并发 |
| 修改 | `deploy/docker-compose.prod.yml` | 确认 prod 拓扑完整（Redis Cluster/RMQ 集群） |

#### 验收标准

- [ ] P95 ≤ 500ms
- [ ] 错误率 < 0.1%
- [ ] WS 不掉线率 > 99.9%
- [ ] 内存/连接无泄漏趋势（2h 稳态）
- [ ] DLQ 增量 ≈ 0
- [ ] 分区巡检通过

---

## 依赖关系总结

```
5.3 大屏 ECharts ────────────────────────────────────────────── 5.4 OEE 仪表盘接入
                                                                    ↑
5.1 IoT Modbus 采集 ──┬── 5.2 停采两端 E2E                          │
                      └── 5.4 真 OEE (A 因子依赖 run_status 传感器) ─┘
                      └── 5.7 IoT 管理补齐 (无强依赖, 可并行)

5.5 前端 3 模块 (完全独立, 可最早启动)
mes-iot CI/Dockerfile (5.1 的前置)
5.6 GA 压测 (最后, 依赖全部完成)
```

## 风险与规避

| 风险 | 影响 | 规避 |
|------|------|------|
| Modbus 协议实现周期超预期 | S1 延期 | 用户已裁决轮询最小实现；modbus_slave_sim.py 先行；5.3/5.5 无依赖可并行 |
| 真 OEE A 因子无数据源 | 5.4 阻塞 | 须在 5.1 中约定 run_status 传感器（寄存器 40002）；若设备无此传感器，A 因子默认 100% |
| 大屏重构回归 | 看板不可用 | 旧版 `<pre>` 渲染保留在 feature 分支对比；截图验收 |
| 前端 3 模块工作量分散 | S1 后半段瓶颈 | 按模块拆分独立提交（iot → quality → integration），每模块 build 通过即提交 |
| prod compose IoT 容器网络 | 容器无法连后端 | healthcheck + depends_on + 同网络；dev 先用 docker-compose 验证 |

---

## 评审要点

请用户确认以下决策点：

1. **实施顺序**: S1 并行 → S2 串行 → S3 验证，是否同意？
2. **图表库选型**: mes-web 质量统计页用 echarts 按需引入（与大屏统一），是否同意？
3. **OEE A 因子传感器约定**: 每设备约定 run_status 布尔传感器（寄存器 40002），若无则 A 默认 100%，是否同意？
4. **iot.json 配置事实源**: devices 从后端 REST 拉取（禁止文件与 DB 双写），iot.json 仅留基础设施配置，是否同意？
5. **5.7 IoT 管理范围**: 告警 resolve/dismiss + 传感器 PUT/DELETE + listAlerts 补字段，设备类型 CRUD 是否需要？（当前 schema 无设备类型表）

---

> **下一步**: 评审通过后按 S1 → S2 → S3 顺序实施，每项独立提交 + 回归（CI 门禁 + 单测 + E2E）。
