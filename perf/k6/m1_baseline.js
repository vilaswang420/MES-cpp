// perf/k6/m1_baseline.js — M1 出口性能基线 (校准版)
// 计划标准: 500 VU 混合场景 (登录 10% / 查询 70% / 报工 20%) 10 分钟,
//           P95 < 300ms, 错误率 < 0.5%。
// 前置: just dev-up 已就绪, hms-backend 运行于 :8088 (可用 HMS_API_BASE 覆盖)。
// 执行: k6 run perf/k6/m1_baseline.js
import http from "k6/http";
import { check } from "k6";
import { Rate } from "k6/metrics";

const BASE = __ENV.HMS_API_BASE || "http://127.0.0.1:8088";
const HEADERS = { "Content-Type": "application/json" };

// 业务层错误率 (响应信封 code != 200), 与 HTTP 层 http_req_failed 分开统计
const apiError = new Rate("api_error");

export const options = {
  vus: 500,
  duration: "10m",
  thresholds: {
    http_req_duration: ["p(95)<300"],
    http_req_failed: ["rate<0.005"],
    api_error: ["rate<0.005"],
  },
};

// ---- setup: 全程 API 建数, 准备一个"进行中"的工单供报工场景使用 ----
export function setup() {
  const suffix = Date.now() % 1000000;
  const login = post("/api/v1/auth/login", { username: "admin", password: "password" });
  const token = login.data.access_token;
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${token}` };

  const product = post("/api/v1/production/products", {
    product_code: `K6-P-${suffix}`, product_name: `K6 基线产品 ${suffix}`, unit: "PCS",
  }, auth).data;
  const line = post("/api/v1/production/lines", {
    line_code: `K6-L-${suffix}`, line_name: `K6 基线产线 ${suffix}`,
  }, auth).data;
  const process = post("/api/v1/production/processes", {
    process_code: `K6-R-${suffix}`, process_name: `K6 工艺 ${suffix}`, product_id: product.id,
    steps: [{ step_seq: 1, step_name: "组装", step_code: `K6-S1-${suffix}` }],
  }, auth).data;
  // 计划量足够大, 10 分钟内报工不会满量完工
  const wo = post("/api/v1/production/work-orders", {
    product_id: product.id, process_id: process.id, line_id: line.id,
    plan_qty: 10000000, priority: 5,
  }, auth).data;
  for (const action of ["schedule", "release", "start"]) {
    http.put(`${BASE}/api/v1/production/work-orders/${wo.id}/${action}`, null, { headers: auth });
  }
  return { token, woId: wo.id };
}

function post(path, body, headers = HEADERS) {
  const res = http.post(`${BASE}${path}`, JSON.stringify(body), { headers });
  let data = {};
  try { data = res.json("code") === 200 ? res.json("data") : {}; } catch (e) { /* ignore */ }
  return { res, data };
}

// 统一响应信封校验: HTTP 200 且 code == 200
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
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${data.token}` };
  const roll = Math.random();

  if (roll < 0.1) {
    // ---- 登录 10% ----
    const { res } = post("/api/v1/auth/login", { username: "admin", password: "password" });
    expectOk(res, "登录");
  } else if (roll < 0.8) {
    // ---- 查询 70% (工单列表分页) ----
    const res = http.get(`${BASE}/api/v1/production/work-orders?page=1&page_size=20`, { headers: auth });
    expectOk(res, "工单列表查询");
  } else {
    // ---- 报工 20% ----
    const res = http.post(
      `${BASE}/api/v1/production/work-orders/${data.woId}/report`,
      JSON.stringify({ step_seq: 1, good_qty: 1 }),
      { headers: auth },
    );
    expectOk(res, "报工");
  }
}
