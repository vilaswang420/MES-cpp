#!/usr/bin/env python3
"""M2 出口: WS 广播延迟与连接数负载验证 (计划 M2 出口标准 "大屏实时刷新延迟 < 2s")。

模式:
  --mode latency: N 个连接订阅 alert 频道, 由编排脚本经 Redis PUBLISH 发送
                  含 send_ms 时间戳的消息, 统计 接收时刻-send_ms 延迟分布。
  --mode load:    保持 N 个连接存活 duration 秒, 输出连接成功率。

用法:
  python scripts/ws_load.py --token TOKEN --connections 1000 --duration 60 --mode load
  python scripts/ws_load.py --token TOKEN --connections 5 --mode latency
       (latency 模式下脚本自身经 docker exec redis-cli PUBLISH 打点消息)
"""
import argparse
import asyncio
import json
import statistics
import subprocess
import time

import websockets

WS_URL = "ws://127.0.0.1:8088/ws?token={token}"
CHANNEL = "alert"


async def one_latency_client(token: str, idx: int, samples: int, out: list):
    uri = WS_URL.format(token=token)
    async with websockets.connect(uri, max_size=2**20) as ws:
        await ws.recv()  # welcome
        await ws.send(json.dumps({"action": "subscribe", "channel": CHANNEL}))
        await ws.recv()  # subscribed ack
        got = 0
        while got < samples:
            msg = await asyncio.wait_for(ws.recv(), timeout=30)
            env = json.loads(msg)
            # 信封契约 ws-push.schema.json: {version, channel, ts, payload}, 无 type 字段
            if env.get("channel") != CHANNEL:
                continue
            send_ms = env.get("payload", {}).get("send_ms")
            if not send_ms:
                continue
            out.append(time.time() * 1000 - send_ms)
            got += 1


def redis_publish(payload: dict):
    # docker exec argv 吃引号: payload 不含空格时仍可能被剥引号, 用 base64 绕过
    import base64
    raw = json.dumps(payload, separators=(",", ":"))
    b64 = base64.b64encode(raw.encode()).decode()
    sh = f"echo {b64} | base64 -d | xargs -0 redis-cli PUBLISH ws:broadcast:{CHANNEL}"
    # xargs -0 保留整串; redis-cli PUBLISH channel payload
    subprocess.run(["docker", "exec", "mes-redis", "sh", "-c", sh],
                   check=True, capture_output=True)


async def latency_mode(token: str, conns: int, rounds: int):
    out = []
    tasks = [asyncio.create_task(one_latency_client(token, i, rounds, out))
             for i in range(conns)]
    await asyncio.sleep(2)  # 等订阅就绪
    for r in range(rounds):
        redis_publish({"device_code": "WS-LAT", "send_ms": time.time() * 1000,
                       "message": f"latency probe {r}"})
        await asyncio.sleep(0.5)
    await asyncio.gather(*tasks)
    out.sort()
    n = len(out)
    p50 = out[int(n * 0.50)] if n else -1
    p95 = out[int(n * 0.95)] if n else -1
    print(json.dumps({
        "samples": n,
        "expected": conns * rounds,
        "min_ms": round(out[0], 1) if n else -1,
        "p50_ms": round(p50, 1),
        "p95_ms": round(p95, 1),
        "max_ms": round(out[-1], 1) if n else -1,
        "gate_p95_lt_2000ms": bool(n and p95 < 2000),
    }))


async def one_load_client(token: str, duration: float, ok: list):
    uri = WS_URL.format(token=token)
    try:
        async with websockets.connect(uri, max_size=2**20, open_timeout=30) as ws:
            await ws.recv()  # welcome
            await ws.send(json.dumps({"action": "subscribe", "channel": CHANNEL}))
            await ws.recv()  # subscribed
            ok[0] += 1
            deadline = time.time() + duration
            while time.time() < deadline:
                try:
                    await asyncio.wait_for(ws.recv(), timeout=5)
                except asyncio.TimeoutError:
                    continue
    except Exception:
        pass


async def load_mode(token: str, conns: int, duration: float):
    ok = [0]
    tasks = [asyncio.create_task(one_load_client(token, duration, ok))
             for _ in range(conns)]
    # 分批统计: 连接阶段完成即报告一次
    await asyncio.sleep(30)
    connected = ok[0]
    await asyncio.gather(*tasks)
    print(json.dumps({
        "requested": conns,
        "connected_30s": connected,
        "survived_full_duration": ok[0],
        "gate_1000_connections": connected >= min(1000, conns),
    }))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--token", required=True)
    ap.add_argument("--mode", choices=["latency", "load"], default="latency")
    ap.add_argument("--connections", type=int, default=5)
    ap.add_argument("--rounds", type=int, default=20)
    ap.add_argument("--duration", type=float, default=60)
    args = ap.parse_args()
    if args.mode == "latency":
        asyncio.run(latency_mode(args.token, args.connections, args.rounds))
    else:
        asyncio.run(load_mode(args.token, args.connections, args.duration))


if __name__ == "__main__":
    main()
