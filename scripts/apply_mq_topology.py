#!/usr/bin/env python3
"""幂等应用 deploy/mq/topology.json 到 RabbitMQ (management HTTP API)。

背景: topology 曾出现 iot.exchange->iot.retry.queue 绑定丢失, 导致毒消息
重试链路断裂 (消息被无绑定丢弃)。本脚本只补缺失项, 不改动已存在实体,
可安全重复执行; 应用后用 scripts/check_mq_topology.py 复验。

注意: queue/exchange 已存在但参数不一致时本脚本不重建 (避免丢消息),
需人工评估后删除重建; 脚本会打印差异提示。

用法: python scripts/apply_mq_topology.py [host] [port] [user] [password]
"""
import base64
import json
import sys
import urllib.error
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = sys.argv[2] if len(sys.argv) > 2 else "15672"
USER = sys.argv[3] if len(sys.argv) > 3 else "hms"
PWD = sys.argv[4] if len(sys.argv) > 4 else "hms_dev_pwd"
VHOST = "%2F"

TOPO = json.load(open("deploy/mq/topology.json", encoding="utf-8"))

AUTH = "Basic " + base64.b64encode(f"{USER}:{PWD}".encode()).decode()


def api(method: str, path: str, body=None):
    url = f"http://{HOST}:{PORT}/api{path}"
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", AUTH)
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status
    except urllib.error.HTTPError as e:
        return e.code


# ---- exchanges ----
real_ex = {}
r = urllib.request.Request(f"http://{HOST}:{PORT}/api/exchanges/{VHOST}")
r.add_header("Authorization", AUTH)
with urllib.request.urlopen(r, timeout=10) as resp:
    real_ex = {e["name"]: e for e in json.load(resp)}

for ex in TOPO["exchanges"]:
    if ex["name"] in real_ex:
        continue
    code = api("PUT", f"/exchanges/{VHOST}/{ex['name']}", {
        "type": ex["type"], "durable": ex["durable"],
        "auto_delete": ex.get("auto_delete", False),
        "internal": ex.get("internal", False),
        "arguments": ex.get("arguments", {}),
    })
    print(f"exchange {ex['name']} -> HTTP {code}")

# ---- queues ----
r = urllib.request.Request(f"http://{HOST}:{PORT}/api/queues/{VHOST}")
r.add_header("Authorization", AUTH)
with urllib.request.urlopen(r, timeout=10) as resp:
    real_q = {q["name"]: q for q in json.load(resp)}

for q in TOPO["queues"]:
    if q["name"] in real_q:
        rargs = real_q[q["name"]].get("arguments") or {}
        for k, v in (q.get("arguments") or {}).items():
            if rargs.get(k) != v:
                print(f"警告: queue {q['name']} 参数 {k} 不符 (期望 {v} 实际 {rargs.get(k)}), "
                      "需人工删除重建")
        continue
    code = api("PUT", f"/queues/{VHOST}/{q['name']}", {
        "durable": q["durable"], "arguments": q.get("arguments", {}),
    })
    print(f"queue {q['name']} -> HTTP {code}")

# ---- bindings (POST 创建; 同 routing_key 重复 POST 幂等返回 204/201) ----
r = urllib.request.Request(f"http://{HOST}:{PORT}/api/bindings/{VHOST}")
r.add_header("Authorization", AUTH)
with urllib.request.urlopen(r, timeout=10) as resp:
    real_b = {(b["source"], b["destination"], b["routing_key"])
              for b in json.load(resp) if b["source"]}

for b in TOPO["bindings"]:
    key = (b["source"], b["destination"], b["routing_key"])
    if key in real_b:
        continue
    code = api("POST", f"/bindings/{VHOST}/e/{b['source']}/q/{b['destination']}",
               {"routing_key": b["routing_key"], "arguments": b.get("arguments", {})})
    print(f"binding {key} -> HTTP {code}")

print("apply 完成, 请运行 check_mq_topology.py 复验")
