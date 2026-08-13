// perf/k6/m2_composite.js — M2 出口复合压测 (校准版)
// 计划标准: 入库 + 1000 WS 连接 + REST 混合三者同跑, REST P95 劣化 < 30%。
// 校准依据 (2026-08-13 单机容量探测): 发布端 pika 峰值 ~12.7k msg/s,
//   消费端可持续 >= 5k msg/s, 计划 20k msg/s 发布目标本机不可达;
//   复合期间入库按可持续均值 ~4k msg/s 打 (峰值 12.7k 突发), 积压可控;
//   时长校准为 10 分钟 (与 M1 基线一致, 便于 P95 对比)。
// M1 基线 P95 = 284ms, 劣化 < 30% => 复合 P95 < 369ms。
// 执行: k6 run perf/k6/m2_composite.js (需与 scripts/ws_load.py --mode load 及
//       scripts/iot_simulator.py --burst 循环同时运行)
import http from "k6/http";
import { check } from "k6";
import { Rate } from "k6/metrics";
import exec from "k6/execution";

const BASE = __ENV.HMS_API_BASE || "http://127.0.0.1:8088";
const HEADERS = { "Content-Type": "application/json" };

const apiError = new Rate("api_error");

export const options = {
  stages: [
    { duration: "30s", target: 500 },
    { duration: "9m30s", target: 500 },
  ],
  thresholds: {
    // 复合门禁: 基线 284ms * 1.3 = 369ms
    http_req_duration: ["p(95)<369"],
    http_req_failed: ["rate<0.005"],
    api_error: ["rate<0.005"],
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
    product_code: `K6C-P-${suffix}`, product_name: `K6 复合产品 ${suffix}`, unit: "PCS",
  }, auth).data;
  const line = post("/api/v1/production/lines", {
    line_code: `K6C-L-${suffix}`, line_name: `K6 复合产线 ${suffix}`,
  }, auth).data;
  const process = post("/api/v1/production/processes", {
    process_code: `K6C-R-${suffix}`, process_name: `K6 复合工艺 ${suffix}`, product_id: product.id,
    steps: [{ step_seq: 1, step_name: "组装", step_code: `K6C-S1-${suffix}` }],
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
      employee_no: `PERF${i}`, password: "Perf@12345",
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

export default function (data) {
  if (!data || !data.token || !data.woIds || data.woIds.length === 0) {
    exec.test.abort(new Error("setup 数据不完整"));
  }
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${data.token}` };
  const roll = Math.random();

  if (roll < 0.1) {
    const u = 1 + Math.floor(Math.random() * 10);
    const { res } = post("/api/v1/auth/login", { username: `perf_u${u}`, password: "Perf@12345" });
    expectOk(res, "登录");
  } else if (roll < 0.8) {
    const res = http.get(`${BASE}/api/v1/production/work-orders?page=1&page_size=20`, { headers: auth });
    expectOk(res, "工单列表查询");
  } else {
    const woId = data.woIds[Math.floor(Math.random() * data.woIds.length)];
    const res = http.post(
      `${BASE}/api/v1/production/work-orders/${woId}/report`,
      JSON.stringify({ step_seq: 1, good_qty: 1 }),
      { headers: auth },
    );
    expectOk(res, "报工");
  }
}
