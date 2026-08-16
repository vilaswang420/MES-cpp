#!/usr/bin/env python3
"""Modbus 从站模拟器 (P4-5.1 E2E 验证)

使用 pymodbus 模拟 Modbus TCP 从站, 提供保持寄存器数据供 mes-iot 采集。
寄存器映射 (与 iot_devices + iot_sensors DB 种子一致):
  40001 (offset 0): 温度传感器 (int16, scale=0.1, 每 2s 变化 20.0~30.0℃)
  40003 (offset 2): 转速传感器 (uint16, scale=1.0, 每 2s 变化 1000~1200 rpm)

用法:
  pip install pymodbus
  python scripts/modbus_slave_sim.py [--host 0.0.0.0] [--port 502] [--unit-id 1]

验证:
  1. 启动模拟器
  2. 启动 mes-iot (config 从 DB 拉取, 设备 IP 指向本机)
  3. 检查 iot_raw_data 表有数据写入 (经 MQ → DataIngestHandler)
"""
import argparse
import random
import threading
import time

try:
    from pymodbus.server import StartTcpServer
    from pymodbus.datastore import ModbusSlaveContext, ModbusServerContext
    from pymodbus.device import ModbusDeviceIdentification
except ImportError:
    print("pymodbus not installed. Install with: pip install pymodbus")
    import sys
    sys.exit(1)


def update_registers(context, unit_id):
    """后台线程: 每 2s 更新寄存器值模拟实时数据"""
    offset = 0  # 寄存器偏移 (40001 → 0)
    while True:
        # 温度: 20.0~30.0℃ (int16, scale=0.1 → 原始值 200~300)
        temp_raw = random.randint(200, 300)
        # 转速: 1000~1200 rpm (uint16, scale=1.0 → 原始值 1000~1200)
        rpm_raw = random.randint(1000, 1200)

        # 写入保持寄存器 (function_code=3, address=offset)
        # pymodbus 内部地址 = Modbus 地址 - 1 (0-based)
        values = [temp_raw, 0, rpm_raw, 0]  # 4 个寄存器 (2 个传感器各占 1 个, 中间留空)
        context[unit_id].setValues(3, offset, values)

        print(f"[sim] register update: temp={temp_raw} (={temp_raw * 0.1}℃), "
              f"rpm={rpm_raw}")

        time.sleep(2)


def main():
    parser = argparse.ArgumentParser(description="Modbus TCP slave simulator")
    parser.add_argument("--host", default="0.0.0.0", help="bind host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=502, help="bind port (default: 502)")
    parser.add_argument("--unit-id", type=int, default=1, help="Modbus unit ID (default: 1)")
    args = parser.parse_args()

    # 初始化寄存器数据
    store = ModbusSlaveContext(
        di=dict(),  # discrete inputs
        co=dict(),  # coils
        hr=dict(),  # holding registers (FC=0x03)
        ir=dict(),  # input registers
        zero_mode=True,  # 0-based addressing
    )
    context = ModbusServerContext(slaves={args.unit_id: store}, single=False)

    # 设备信息
    identity = ModbusDeviceIdentification()
    identity.VendorName = "MES Simulator"
    identity.ProductName = "Modbus Slave Sim"
    identity.ProductVersion = "1.0"

    # 启动寄存器更新线程
    updater = threading.Thread(
        target=update_registers, args=(context, args.unit_id), daemon=True
    )
    updater.start()

    print(f"[sim] Modbus TCP slave starting on {args.host}:{args.port} "
          f"(unit_id={args.unit_id})")
    print("[sim] Register map:")
    print("  40001 (offset 0): temperature (int16, scale=0.1)")
    print("  40003 (offset 2): rpm (uint16, scale=1.0)")

    # 启动 TCP 服务器 (阻塞)
    StartTcpServer(
        context=context,
        identity=identity,
        address=(args.host, args.port),
    )


if __name__ == "__main__":
    main()
