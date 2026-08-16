// perf/k6/p2_report_concurrency.js — P2-3.1 报工并发稳定性专项
// 目标: 验证单 SQL CTE 原子防超报 (completed_qty + delta <= plan_qty) 在
//       高并发下不超报: 120 个并发报工打同一 plan_qty=100 工单,
//       恰好 100 次成功 (满量自动完工), 其余 20 次被原子拦截返回 409。
// 前置: just dev-up 已就绪, hms-backend 运行于 :8088 (可用 HMS_API_BASE 覆盖)。
// 执行: k6 run perf/k6/p2_report_concurrency.js
// 退出判定: 门禁阈值 + teardown DB/API 断言 completed_qty == plan_qty (不超报)。
import http from "k6/http";
import { check } from "k6";
import { Rate } from "k6/metrics";
import exec from "k6/execution";

const BASE = __ENV.HMS_API_BASE || "http://127.0.0.1:8088";
const HEADERS = { "Content-Type": "application/json" };

// 业务层意外错误率 (HTTP 200 但 code != 200/409, 或 5xx): 并发保护不应产生 500
const apiError = new Rate("api_error");
// 409 冲突率 (超报被原子拦截属预期行为, 单独统计)
const conflictRate = new Rate("over_report_conflict");

export const options = {
  // 单轮 120 个并发迭代: 每 VU 恰报 1 次 (good_qty=1), 120 = plan 100 + 20 超额
  vus: 120,
  iterations: 120,
  thresholds: {
    // 5xx/连接错误必须为 0 (并发原子防护不应抛 500); 409 不计入 http_req_failed
    http_req_failed: ["rate<0.005"],
    api_error: ["rate<0.005"],
  },
};

export function setup() {
  const suffix = Date.now() % 1000000;
  const login = post("/api/v1/auth/login", { username: "admin", password: "password" });
  const token = login.data && login.data.access_token;
  if (!token) throw new Error("setup 登录失败: HTTP " + login.res.status);
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${token}` };

  const product = post("/api/v1/production/products", {
    product_code: `K6-PC-${suffix}`, product_name: `并发报工产品 ${suffix}`, unit: "PCS",
  }, auth).data;
  const line = post("/api/v1/production/lines", {
    line_code: `K6-LC-${suffix}`, line_name: `并发报工产线 ${suffix}`,
  }, auth).data;
  const process = post("/api/v1/production/processes", {
    process_code: `K6-RC-${suffix}`, process_name: `并发报工工艺 ${suffix}`, product_id: product.id,
    steps: [{ step_seq: 1, step_name: "组装", step_code: `K6-SC1-${suffix}` }],
  }, auth).data;
  const wo = post("/api/v1/production/work-orders", {
    product_id: product.id, process_id: process.id, line_id: line.id,
    plan_qty: 100, priority: 5, // 小计划量: 并发报工 100 次即满量, 便于精确断言
  }, auth).data;
  if (!wo.id) throw new Error("setup 建单失败");
  for (const action of ["schedule", "release", "start"]) {
    const r = http.put(`${BASE}/api/v1/production/work-orders/${wo.id}/${action}`, null,
      { headers: auth });
    if (r.status !== 200) throw new Error(`流转 ${action} 失败: HTTP ${r.status}`);
  }
  return { token, woId: wo.id };
}

function post(path, body, headers = HEADERS) {
  const res = http.post(`${BASE}${path}`, JSON.stringify(body), { headers });
  let data = {};
  try { data = res.json("code") === 200 ? res.json("data") : {}; } catch (e) { /* ignore */ }
  return { res, data };
}

export default function (data) {
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${data.token}` };
  const res = http.post(
    `${BASE}/api/v1/production/work-orders/${data.woId}/report`,
    JSON.stringify({ step_seq: 1, good_qty: 1 }),
    { headers: auth },
  );
  let code = 0;
  try { code = res.json("code"); } catch (e) { /* ignore */ }

  if (res.status === 200 && code === 200) {
    check(res, { "report 成功 (code=200)": () => true });
  } else if (res.status === 409 || code === 409) {
    conflictRate.add(1); // 超报被原子拦截: 预期行为
    check(res, { "report 超报拦截 (409)": () => true });
  } else {
    apiError.add(1); // 意外错误: 并发保护不应产生 500/其他
    check(res, { [`report 意外错误 (HTTP ${res.status}, code=${code})`]: () => false });
  }
}

export function teardown(data) {
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${data.token}` };
  const res = http.get(`${BASE}/api/v1/production/work-orders/${data.woId}`, { headers: auth });
  const detail = res.json("data") || {};
  const completed = detail.completed_qty ?? -1;
  const status = detail.status ?? -1;
  // 核心验收: 并发 120 报 100 计划, completed_qty 恰为 100 (不超报), 且自动完工 status=5
  if (completed !== 100) {
    exec.test.abort(new Error(`并发报工超报/漏报: completed_qty=${completed}, 期望 100`));
  }
  if (status !== 5) {
    exec.test.abort(new Error(`满量未自动完工: status=${status}, 期望 5`));
  }
  console.log(`PASS: 并发 120 报工 -> completed_qty=${completed} (不超报), status=${status} 已完工`);
}
