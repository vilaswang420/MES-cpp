#!/usr/bin/env python3
"""IoT 数据模拟器 (计划任务 18): 直接向 RabbitMQ 发布符合
contracts/iot-message.schema.json 的数据消息, 使后端入库链路开发不被硬件阻塞。

依赖: pip install pika

用法:
  python scripts/iot_simulator.py --count 10000 --devices 100
  python scripts/iot_simulator.py --poison 3        # 投毒消息 (验证有界重试 -> DLQ)
"""
import argparse
import json
import random
import time
from datetime import datetime, timezone

try:
    import pika
except ImportError:
    raise SystemExit("缺少依赖 pika, 请先执行: pip install pika")

EXCHANGE = "iot.exchange"
ROUTING_KEY = "data.report"
POISON_ROUTING_KEY = "data.report"  # 同路由, 靠 payload 非法触发消费端重试


def now_utc_iso() -> str:
    # 全系统时区约定: UTC ISO8601 带 Z
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


def make_message(device_id: int, sensor_id: int) -> dict:
    return {
        "version": "1.0",
        "device_id": device_id,
        "device_code": f"DEV-SIM-{device_id:04d}",
        "sensor_id": sensor_id,
        "value": round(random.uniform(20.0, 90.0), 2),
        "quality": 192,  # OPC-UA Good
        "ts": now_utc_iso(),
    }


def make_poison_message() -> dict:
    # 缺 required 字段 + 非法类型: 消费端 schema 校验失败, 3 次重试后应进 DLQ
    return {"version": "1.0", "device_code": "DEV-POISON", "value": "not-a-number"}


def main() -> None:
    ap = argparse.ArgumentParser(description="HMS IoT 数据模拟器")
    ap.add_argument("--url", default="amqp://hms:hms_dev_pwd@127.0.0.1:5672/")
    ap.add_argument("--count", type=int, default=1000, help="正常消息总数")
    ap.add_argument("--devices", type=int, default=10, help="模拟设备数")
    ap.add_argument("--sensors-per-device", type=int, default=3)
    ap.add_argument("--interval-ms", type=int, default=0, help="每条消息间隔 (0=全速)")
    ap.add_argument("--poison", type=int, default=0,
                    help="额外投毒消息条数 (M2 DLQ 验证)")
    args = ap.parse_args()

    conn = pika.BlockingConnection(pika.URLParameters(args.url))
    ch = conn.channel()
    # exchange 由 deploy/mq/topology.json 预声明; passive 校验存在性
    ch.exchange_declare(EXCHANGE, exchange_type="topic",
                        durable=True, passive=True)
    ch.confirm_delivery()

    sent = 0
    start = time.monotonic()
    for i in range(args.count):
        device_id = i % args.devices + 1
        sensor_id = i % args.sensors_per_device + 1
        body = json.dumps(make_message(device_id, sensor_id))
        ch.basic_publish(
            EXCHANGE,
            ROUTING_KEY,
            body,
            pika.BasicProperties(
                content_type="application/json", delivery_mode=2),
            mandatory=True,
        )
        sent += 1
        if args.interval_ms > 0:
            time.sleep(args.interval_ms / 1000.0)
        if sent % 2000 == 0:
            print(f"  published {sent}/{args.count}")

    for i in range(args.poison):
        body = json.dumps(make_poison_message())
        ch.basic_publish(
            EXCHANGE,
            POISON_ROUTING_KEY,
            body,
            pika.BasicProperties(
                content_type="application/json", delivery_mode=2),
        )
        print(f"  poison message #{i + 1} published (应经 3 次重试后进 iot.dlq)")

    elapsed = time.monotonic() - start
    conn.close()
    print(
        f"done: {sent} messages in {elapsed:.2f}s ({sent / max(elapsed, 0.001):.0f} msg/s)")


if __name__ == "__main__":
    main()
