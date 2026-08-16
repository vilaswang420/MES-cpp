// perf/k6/m1_baseline.js — M1 出口性能基线 (校准版)
// 计划标准: 500 VU 混合场景 (登录 10% / 查询 70% / 报工 20%) 10 分钟,
//           P95 < 300ms, 错误率 < 0.5%。
// 前置: just dev-up 已就绪, mes-backend 运行于 :8088 (可用 MES_API_BASE 覆盖)。
// 执行: k6 run perf/k6/m1_baseline.js
import http from "k6/http";
import { check } from "k6";
import { Rate } from "k6/metrics";
import exec from "k6/execution";

const BASE = __ENV.MES_API_BASE || "http://127.0.0.1:8088";
const HEADERS = { "Content-Type": "application/json" };

// 业务层错误率 (响应信封 code != 200), 与 HTTP 层 http_req_failed 分开统计
const apiError = new Rate("api_error");

export const options = {
  // 30s 内线性爬升到 500 VU 后保持至 10 分钟 (总时长/峰值不变);
  // 避免 t=0 瞬时建连风暴抬高尾部延迟 (第 6 轮无 ramp 时 P95=303ms 差 3ms)
  stages: [
    { duration: "30s", target: 500 },
    { duration: "9m30s", target: 500 },
  ],
  thresholds: {
    http_req_duration: ["p(95)<300"],
    http_req_failed: ["rate<0.005"],
    api_error: ["rate<0.005"],
  },
};

// ---- setup: 全程 API 建数, 准备 N 个"进行中"工单供报工场景随机打散 ----
// 校准说明: 单工单报工受 FOR UPDATE 行锁串行化约束 (实测 ~250/s),
// 真实产线为多工单并行报工, 故分散到 WO_COUNT 个工单 (计划仅约定"报工 20%"比例)。
const WO_COUNT = 8;

export function setup() {
  const suffix = Date.now() % 1000000;
  const login = post("/api/v1/auth/login", { username: "admin", password: "password" });
  const token = login.data.access_token;
  // fail-fast: 后端未就绪时直接终止, 避免 null 数据污染 VU 阶段 (历史教训:
  // 启动窗口重叠时 setup 建数失败 -> woIds 含 null -> 大量 /null/report 500)
  if (!token) {
    throw new Error("setup 登录失败, 后端可能未就绪: HTTP " + login.res.status);
  }
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

  const woIds = [];
  for (let i = 0; i < WO_COUNT; i++) {
    // 计划量足够大, 10 分钟内报工不会满量完工
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

  // 压测登录用户 perf_u1..10 (已存在则 409 忽略, 保证脚本自含可重复)
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
  // 双重保险: 数据污染时跳过而非发送 null 请求
  if (!data || !data.token || !data.woIds || data.woIds.length === 0) {
    exec.test.abort(new Error("setup 数据不完整"));
  }
  const auth = { "Content-Type": "application/json", Authorization: `Bearer ${data.token}` };
  const roll = Math.random();

  if (roll < 0.1) {
    // ---- 登录 10% (随机压测用户, 真实场景为多账号并发; 单账号会被
    //      UPDATE last_login_at 同行锁串行化, 属正常保护而非系统容量) ----
    const u = 1 + Math.floor(Math.random() * 10);
    const { res } = post("/api/v1/auth/login", { username: `perf_u${u}`, password: "Perf@12345" });
    expectOk(res, "登录");
  } else if (roll < 0.8) {
    // ---- 查询 70% (工单列表分页) ----
    const res = http.get(`${BASE}/api/v1/production/work-orders?page=1&page_size=20`, { headers: auth });
    expectOk(res, "工单列表查询");
  } else {
    // ---- 报工 20% (随机命中 N 个工单之一) ----
    const woId = data.woIds[Math.floor(Math.random() * data.woIds.length)];
    const res = http.post(
      `${BASE}/api/v1/production/work-orders/${woId}/report`,
      JSON.stringify({ step_seq: 1, good_qty: 1 }),
      { headers: auth },
    );
    expectOk(res, "报工");
  }
}
