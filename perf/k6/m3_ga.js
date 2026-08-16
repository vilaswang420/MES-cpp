// perf/k6/m3_ga.js — 5.6 GA 标准环境 2h 容量验证 (P4 Sprint 3)
// 计划验收 (docs/CORE_PLAN.md 5.6):
//   P95 <= 500ms | 错误率 < 0.1% | WS 1000 连接不掉线率 > 99.9% | 内存/连接无泄漏趋势
// 场景组成 (复合, 同跑):
//   rest : REST 读写混合 (登录 10% / 工单列表 70% / 报工 20%), VU 数可调
//   ws   : 1000 个 WebSocket 连接全程保持 (per-vu-iterations, 每 VU 1 连接握到底)
// 入库负载 (~4k msg/s) 不在本脚本内: 按 RUNBOOK 用 scripts/iot_simulator.py --burst 并行打。
//
// 与 m2_composite.js (10min 校准版) 的差异:
//   1. duration 参数化: MES_GA_SECONDS (默认 7200 = 2h), 便于 smoke/全量复用同一脚本
//   2. JWT 2h 过期 -> REST 每 VU 本地 token 90min 自动轮换 (MES_GA_TOKEN_TTL_S)
//      (WS 仅握手时验 token, 连接建立后存活整个时长, 无需轮换)
//   3. WS 1000 连接改为 k6 原生 ws 场景, 断连率进 thresholds (不再依赖外部 ws_load.py)
//   4. 阈值按 GA 口径收紧: 错误率 0.5% -> 0.1%, P95 369ms(基线*1.3) -> 500ms(GA 线)
//
// 用法:
//   smoke (10min): k6 run -e MES_GA_SECONDS=600 -e MES_GA_WS_VUS=100 perf/k6/m3_ga.js
//   全量  (2h)   : k6 run -e MES_API_BASE=http://<server>:8088 perf/k6/m3_ga.js
//   (执行步骤/环境要求见 perf/k6/M3_GA_RUNBOOK.md; 泄漏趋势另跑 ga_leak_monitor.py)
import http from "k6/http";
import ws from "k6/ws";
import { check, sleep } from "k6";
import { Counter, Rate } from "k6/metrics";
import exec from "k6/execution";

const BASE = __ENV.MES_API_BASE || "http://127.0.0.1:8088";
const WS_BASE = __ENV.MES_WS_BASE || BASE.replace(/^http/, "ws");
const HEADERS = { "Content-Type": "application/json" };

// ---- 参数 (全部可用 -e 覆盖, 默认值 = GA 标准口径) ----
const GA_SECONDS = parseInt(__ENV.MES_GA_SECONDS || "7200", 10); // 总稳态时长, 默认 2h
const REST_VUS = parseInt(__ENV.MES_GA_REST_VUS || "400", 10);
const WS_VUS = parseInt(__ENV.MES_GA_WS_VUS || "1000", 10);
const TOKEN_TTL_MS = parseInt(__ENV.MES_GA_TOKEN_TTL_S || "5400", 10) * 1000; // 90min < 2h JWT
const RAMP_UP_S = parseInt(__ENV.MES_GA_RAMP_S || "300", 10); // 5min 爬坡

const HOLD_S = GA_SECONDS - RAMP_UP_S - 120; // 爬坡+降坡之外的稳态保持
if (HOLD_S <= 0) throw new Error(`MES_GA_SECONDS=${GA_SECONDS} 太短 (需 > 爬坡${RAMP_UP_S}s + 降坡120s)`);

const apiError = new Rate("api_error");
const wsConnects = new Counter("ws_connects");
const wsDropped = new Rate("ws_dropped"); // 意外断连占连接数比例

export const options = {
  scenarios: {
    rest: {
      executor: "ramping-vus",
      startVUs: 0,
      stages: [
        { duration: `${RAMP_UP_S}s`, target: REST_VUS },
        { duration: `${HOLD_S}s`, target: REST_VUS },
        { duration: "2m", target: 0 },
      ],
      gracefulRampDown: "30s",
      exec: "rest",
    },
    ws: {
      executor: "per-vu-iterations", // 每 VU 恰好 1 次迭代 = 1 条连接握到底
      vus: WS_VUS,
      iterations: 1,
      startTime: "1m", // 等 REST 爬坡先起一部分, 避免握手风暴叠加
      maxDuration: `${GA_SECONDS + 600}s`,
      exec: "wsHold",
    },
  },
  thresholds: {
    // GA 门禁 (5.6 验收): P95<=500ms, 错误率<0.1%, WS 不掉线率>99.9%
    "http_req_duration{scenario:rest}": ["p(95)<500"],
    "http_req_failed{scenario:rest}": ["rate<0.001"],
    api_error: ["rate<0.001"],
    wsDropped: ["rate<0.001"],
    checks: ["rate>0.999"],
  },
};

const WO_COUNT = 8;

export function setup() {
  const suffix = Date.now() % 1000000;
  const login = post("/api/v1/auth/login", { username: "admin", password: "password" });
  const token = login.data.access_token;
  if (!token) {
    throw new Error("setup 登录失败, 后端可能未就绪: HTTP " + login.res.status);
  }
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${token}` };

  const product = post("/api/v1/production/products", {
    product_code: `K6G-P-${suffix}`, product_name: `GA 产品 ${suffix}`, unit: "PCS",
  }, auth).data;
  const line = post("/api/v1/production/lines", {
    line_code: `K6G-L-${suffix}`, line_name: `GA 产线 ${suffix}`,
  }, auth).data;
  const process = post("/api/v1/production/processes", {
    process_code: `K6G-R-${suffix}`, process_name: `GA 工艺 ${suffix}`, product_id: product.id,
    steps: [{ step_seq: 1, step_name: "组装", step_code: `K6G-S1-${suffix}` }],
  }, auth).data;

  const woIds = [];
  for (let i = 0; i < WO_COUNT; i++) {
    const wo = post("/api/v1/production/work-orders", {
      product_id: product.id, process_id: process.id, line_id: line.id,
      plan_qty: 10000000, priority: 5,
    }, auth).data;
    if (!wo.id) throw new Error(`setup 建单失败 (第 ${i + 1} 个), 中止以免污染压测数据`);
    for (const action of ["schedule", "release", "start"]) {
      http.put(`${BASE}/api/v1/production/work-orders/${wo.id}/${action}`, null, { headers: auth });
    }
    woIds.push(wo.id);
  }

  for (let i = 1; i <= 10; i++) {
    post("/api/v1/system/users", {
      username: `perf_u${i}`, real_name: `压测用户${i}`,
      employee_no: `GA${i}`, password: "Perf@12345",
    }, auth);
  }
  return { token, woIds };
}

function post(path, body, headers = HEADERS) {
  const res = http.post(`${BASE}${path}`, JSON.stringify(body), { headers });
  let data = {};
  try { data = res.json("code") === 200 ? res.json("data") : {}; } catch (e) { /* ignore */ }
  return { res, data };
}

function expectOk(res, name) {
  let codeOk = false;
  try { codeOk = res.json("code") === 200; } catch (e) { /* ignore */ }
  apiError.add(!codeOk);
  check(res, {
    [`${name}: HTTP 200`]: (r) => r.status === 200,
    [`${name}: code=200`]: () => codeOk,
  });
  return codeOk;
}

// ---- REST 场景: 每 VU 本地 token, 到期 (默认 90min) 自动重登 ----
let vuToken = null;
let vuTokenAt = 0;

function ensureToken() {
  if (vuToken && Date.now() - vuTokenAt < TOKEN_TTL_MS) return true;
  const { res, data } = post("/api/v1/auth/login", { username: "admin", password: "password" });
  const ok = res.status === 200 && data.access_token;
  if (ok) { vuToken = data.access_token; vuTokenAt = Date.now(); }
  else { apiError.add(true); check(null, { "token 轮换登录成功": () => false }); }
  return ok;
}

export function rest(data) {
  if (!data || !data.token || !data.woIds || data.woIds.length === 0) {
    exec.test.abort(new Error("setup 数据不完整"));
  }
  if (!ensureToken()) return;
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${vuToken}` };
  const roll = Math.random();

  if (roll < 0.1) {
    const u = 1 + Math.floor(Math.random() * 10);
    const { res } = post("/api/v1/auth/login", { username: `perf_u${u}`, password: "Perf@12345" });
    expectOk(res, "登录");
  } else if (roll < 0.8) {
    const res = http.get(`${BASE}/api/v1/production/work-orders?page=1&page_size=20`, { headers: auth });
    // 401 = token 恰好到期, 立即轮换重试一次 (不计错误)
    if (res.status === 401) { vuToken = null; return; }
    expectOk(res, "工单列表查询");
  } else {
    const woId = data.woIds[Math.floor(Math.random() * data.woIds.length)];
    const res = http.post(
      `${BASE}/api/v1/production/work-orders/${woId}/report`,
      JSON.stringify({ step_seq: 1, good_qty: 1 }),
      { headers: auth },
    );
    if (res.status === 401) { vuToken = null; return; }
    expectOk(res, "报工");
  }
  sleep(1); // 节流: 400 VU * ~1 req/s = ~400 rps 稳态
}

// ---- WS 场景: 1 连接/ VU, 全程保持, protocol ping 30s 保活 ----
export function wsHold(data) {
  if (!data || !data.token) exec.test.abort(new Error("setup 数据不完整"));
  const url = `${WS_BASE}/ws?token=${data.token}`;
  const start = Date.now();
  const targetMs = GA_SECONDS * 1000;
  let connectFail = true;

  const res = ws.connect(url, {}, (socket) => {
    socket.on("open", () => {
      connectFail = false;
      wsConnects.add(1);
      socket.send(JSON.stringify({
        action: "subscribe",
        channels: ["production.realtime", "device.status", "alert", "workorder.event"],
      }));
    });
    socket.on("message", () => { /* 广播消息直接丢弃, 只需排空接收缓冲 */ });
    socket.setInterval(() => socket.ping(), 30000); // Drogon/trantor 自动回 pong
  });

  // ws.connect 在连接关闭后返回: elapsed < 目标时长 => 意外掉线
  const elapsed = Date.now() - start;
  wsDropped.add(connectFail || elapsed < targetMs - 60000);
  check(res, {
    "WS 连接建立": (r) => r && r.status === 101,
  });
}
