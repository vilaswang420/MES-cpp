#!/usr/bin/env python3
"""M2 门禁: 校验 RabbitMQ 实际拓扑与 deploy/mq/topology.json 声明一致。

通过 management HTTP API (15672) 拉取 exchanges/queues/bindings,
逐项比对名称、类型、durable、关键 arguments 与 binding routing_key。
用法: python scripts/check_mq_topology.py [host] [port] [user] [password]
退出码: 0=一致, 1=存在差异。
"""
import base64
import json
import sys
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = sys.argv[2] if len(sys.argv) > 2 else "15672"
USER = sys.argv[3] if len(sys.argv) > 3 else "mes"
PWD = sys.argv[4] if len(sys.argv) > 4 else "mes_dev_pwd"
VHOST = "%2F"

TOPO = json.load(open("deploy/mq/topology.json", encoding="utf-8"))

errors = []


def api(path: str):
    url = f"http://{HOST}:{PORT}/api{path}"
    req = urllib.request.Request(url)
    req.add_header(
        "Authorization", "Basic " + base64.b64encode(f"{USER}:{PWD}".encode()).decode()
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.load(resp)


# ---- exchanges ----
real_ex = {(e["name"], e["type"], e["durable"]) for e in api(f"/exchanges/{VHOST}")}
for ex in TOPO["exchanges"]:
    key = (ex["name"], ex["type"], ex["durable"])
    if key not in real_ex:
        errors.append(f"exchange 缺失或不符: {key}")

# ---- queues (名称 + durable + 关键 arguments) ----
real_q = {q["name"]: q for q in api(f"/queues/{VHOST}")}
for q in TOPO["queues"]:
    rq = real_q.get(q["name"])
    if not rq:
        errors.append(f"queue 缺失: {q['name']}")
        continue
    if rq.get("durable") != q["durable"]:
        errors.append(f"queue {q['name']} durable 不符: 期望 {q['durable']} 实际 {rq.get('durable')}")
    rargs = rq.get("arguments") or {}
    for k, v in (q.get("arguments") or {}).items():
        if rargs.get(k) != v:
            errors.append(f"queue {q['name']} 参数 {k} 不符: 期望 {v} 实际 {rargs.get(k)}")

# ---- bindings (source -> destination + routing_key) ----
real_b = {
    (b["source"], b["destination"], b["routing_key"])
    for b in api(f"/bindings/{VHOST}")
    if b["source"]  # 排除默认 exchange 的自绑定
}
for b in TOPO["bindings"]:
    key = (b["source"], b["destination"], b["routing_key"])
    if key not in real_b:
        errors.append(f"binding 缺失: {key}")

if errors:
    print("MQ 拓扑校验失败:")
    for e in errors:
        print("  - " + e)
    sys.exit(1)

print(
    f"MQ 拓扑校验通过: {len(TOPO['exchanges'])} exchange / "
    f"{len(TOPO['queues'])} queue / {len(TOPO['bindings'])} binding 与 topology.json 一致"
)
