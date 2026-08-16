#!/usr/bin/env python3
"""ERP/WMS 本地桩 (WireMock 风格, 计划任务 23 "桩先行")。

用途: MES IntegrationService 外呼的对端替身, dev/冒烟/熔断与 Saga 补偿验证。
监听 127.0.0.1:9095 (与迁移 009 的 integ_api_configs 种子一致)。

故障注入控制端 (冒烟脚本用):
  POST /__control {"mode": "fail_wms", "count": N}   # 接下来 N 次 WMS stock-in 返回 500
  POST /__control {"mode": "fail_erp_orders", "count": N}  # 接下来 N 次订单同步返回 500
  POST /__control {"mode": "normal"}                 # 清零故障
  GET  /__control/state                              # 当前计数器

用法: python scripts/erp_wms_stub.py [port]
"""
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATE = {"fail_wms": 0, "fail_erp_orders": 0, "calls": {}}
LOCK = threading.Lock()


def bump(path: str) -> int:
    with LOCK:
        STATE["calls"][path] = STATE["calls"].get(path, 0) + 1
        return STATE["calls"][path]


class StubHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    # ---------- 响应辅助 ----------
    def _reply(self, code: int, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw) if raw else {}
        except Exception:
            return {}

    def log_message(self, fmt, *args):  # 静音默认访问日志
        pass

    # ---------- GET ----------
    def do_GET(self):
        path = self.path.split("?")[0]
        bump(path)
        if path == "/erp/orders":
            with LOCK:
                if STATE["fail_erp_orders"] > 0:
                    STATE["fail_erp_orders"] -= 1
                    return self._reply(500, {"error": "stub: ERP 故障注入"})
            # 确定性订单集: 时间窗内每天 2 单, 供增量同步/转工单冒烟
            orders = []
            for i in range(1, 6):
                orders.append({
                    "order_no": f"ERP-STUB-{i:04d}",
                    "order_type": 1,
                    "product_code": "STUB-P1",
                    "product_name": f"桩产品 {i}",
                    "quantity": 100 + i,
                    "unit": "PCS",
                    "plan_start_date": "2026-08-01",
                    "plan_end_date": "2026-08-31",
                    "priority": 5,
                    "customer_name": "桩客户",
                })
            return self._reply(200, {"orders": orders})
        if path == "/__control/state":
            with LOCK:
                return self._reply(200, json.loads(json.dumps(STATE)))
        return self._reply(404, {"error": "not found"})

    # ---------- POST ----------
    def do_POST(self):
        path = self.path.split("?")[0]
        body = self._read_body()
        bump(path)
        if path == "/__control":
            mode = body.get("mode", "normal")
            with LOCK:
                if mode == "fail_wms":
                    STATE["fail_wms"] = int(body.get("count", 1))
                elif mode == "fail_erp_orders":
                    STATE["fail_erp_orders"] = int(body.get("count", 1))
                elif mode == "normal":
                    STATE["fail_wms"] = 0
                    STATE["fail_erp_orders"] = 0
                return self._reply(200, {"ok": True, "mode": mode})
        if path == "/wms/stock-in":
            with LOCK:
                if STATE["fail_wms"] > 0:
                    STATE["fail_wms"] -= 1
                    return self._reply(500, {"error": "stub: WMS 故障注入"})
            return self._reply(200, {"code": 0, "wms_no": "WMS-IN-STUB-1",
                                     "message": "入库成功", "echo": body})
        if path in ("/wms/pick-request", "/wms/stock-in/cancel",
                    "/erp/work-orders") or path.startswith("/erp/work-orders/"):
            if path.endswith("/completion-report"):
                return self._reply(200, {"code": 0, "message": "回报已受理", "echo": body})
            if path.endswith("/report-cancel"):
                return self._reply(200, {"code": 0, "message": "回报已撤销", "echo": body})
            if path == "/wms/pick-request":
                return self._reply(200, {"code": 0, "wms_no": "WMS-PICK-STUB-1",
                                         "message": "领料已受理", "echo": body})
            return self._reply(200, {"code": 0, "echo": body})
        return self._reply(404, {"error": "not found"})


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9095
    srv = ThreadingHTTPServer(("127.0.0.1", port), StubHandler)
    print(f"erp_wms_stub listening on 127.0.0.1:{port}", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
