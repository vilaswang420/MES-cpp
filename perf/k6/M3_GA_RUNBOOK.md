# M3 GA 2h 容量验证运行手册 (P4-5.6)

> 对应 `docs/CORE_PLAN.md` 5.6。脚本: `perf/k6/m3_ga.js` + `perf/k6/ga_leak_monitor.py`。
> 本手册描述在**标准服务器**上执行 GA 验证的完整步骤; 本机仅可用于 smoke 冒烟。

## 1. 验收门禁 (自动判定)

| 指标 | 阈值 | 判定来源 |
|------|------|----------|
| REST P95 | <= 500 ms | k6 threshold `http_req_duration{scenario:rest}` |
| HTTP 错误率 | < 0.1% | k6 threshold `http_req_failed` + `api_error` |
| WS 不掉线率 | > 99.9% | k6 threshold `ws_dropped` (1000 连接全程保持) |
| 内存/连接泄漏 | 无持续增长趋势 | `ga_leak_monitor.py` 线性回归判定 (exit 0) |

任一 threshold 失败 k6 退出码非 0; 泄漏监控独立退出码 (2=疑似泄漏)。

## 2. 环境要求 (GA 必须 prod 形态)

- 服务器 >= 8C16G, 与生产同规格; OS Linux (监控脚本依赖 /proc)
- **`deploy/compose/docker-compose.prod.yml`** 启动 (Redis/RMQ 集群行为与 dev 单机差异大, 不得用 dev compose)
- 后端 Release 构建 (`-DCMAKE_BUILD_TYPE=Release`), 迁移 001-017 全部执行
- k6 >= 0.45 (需 `k6/ws` 与 scenarios); 压测机与服务器分开部署或至少 8C 以上余量
- 大屏 WS 频道广播链路 (Redis Pub/Sub) 正常

容量基线 (与 M1/M2 同机对比时参考): M1 基线 P95=284ms, M2 复合 10min P95<369ms, WS 1000 连接通过 (本机)。

## 3. 执行步骤

```bash
# 0) 前置: 服务就绪检查 (登录返回 access_token)
curl -s http://127.0.0.1:8088/api/v1/auth/login -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"password"}'

# 1) 启动泄漏监控 (独立终端, 全程跑; pidof 若为容器内进程用 docker exec + /proc/1)
python3 perf/k6/ga_leak_monitor.py --pid $(pidof mes-backend) --port 8088 \
  --duration 7500 --out ga_leak.csv          # 比压测多留 5min 观察收尾

# 2) 入库背景负载 (~4k msg/s 可持续, M2 校准值; 模拟 IoT 采集)
python3 scripts/iot_simulator.py --burst &   # 参数见脚本头部, 保持与 M2 相同口径

# 3) smoke 冒烟 (10min, 先验证脚本与环境, 不出正式结论)
k6 run -e MES_API_BASE=http://127.0.0.1:8088 \
       -e MES_GA_SECONDS=600 -e MES_GA_REST_VUS=50 -e MES_GA_WS_VUS=100 \
       perf/k6/m3_ga.js

# 4) 正式 2h 全量 (默认参数即 GA 口径: 400 REST VU + 1000 WS + 2h)
k6 run -e MES_API_BASE=http://127.0.0.1:8088 perf/k6/m3_ga.js \
  --summary-export ga_result.json | tee ga_console.log

# 5) 收尾: 等泄漏监控结束输出判定; 汇总三份产物
#    ga_result.json / ga_console.log / ga_leak.csv
```

## 4. 脚本行为说明 (执行前必读)

- **JWT 2h 过期**: REST 场景每 VU 本地 token, 默认 90min 自动轮换 (`MES_GA_TOKEN_TTL_S`); 收到 401 时立即作废重登 (该次请求不计错误)。WS 仅握手时验 token, 连接建立后不受过期影响。
- **WS 保活**: 每 30s 发 protocol 层 ping (Drogon 自动回 pong), 不依赖应用层消息。
- **掉线判定**: 连接存活 < 总时长-60s 即计掉线 (k6 `ws.connect` 在连接关闭后才返回)。
- **节流**: REST 每 VU sleep 1s, 稳态 ~400 rps; 2h 报工约 57 万条 (setup 建单 plan_qty=10,000,000 足够)。
- **环境变量**: `MES_GA_SECONDS / MES_GA_REST_VUS / MES_GA_WS_VUS / MES_GA_RAMP_S / MES_GA_TOKEN_TTL_S / MES_WS_BASE`。

## 5. 泄漏判定口径

`ga_leak_monitor.py` 对 RSS / fd 数 / 目标端口 TCP established 三条曲线做最小二乘回归:

- RSS: 斜率 > 20 MB/h 且 后 1/4 均值 > 前 1/4 均值 × 1.10 → 疑似泄漏
- fd: 斜率 > 100/h 且后段 > 前段 + 50 → 疑似泄漏
- TCP: 斜率 > 50/h 且后段 > 前段 + 20 → 疑似泄漏 (WS 1000 稳态本身持有 ~1000 条, 看**斜率**不看绝对值)

疑似泄漏时用 CSV 画曲线复核 (是否阶梯型/单次跳变 vs 持续线性增长)。

## 6. 结果记录模板

```
GA 验证日期 / 服务器规格 / compose 形态:
k6 版本 / 并发参数 (REST VU, WS VU, 时长):
P95 / P99 / 错误率 / TPS:
WS 掉线率 (ws_dropped):
泄漏判定 (RSS/fd/TCP 斜率):
结论: 通过 / 不通过 (原因)
产物: ga_result.json, ga_console.log, ga_leak.csv
```
