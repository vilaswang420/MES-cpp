# mes-iot — MES IoT 数据采集服务

独立的 C++ 采集服务（M2 子系统），从设备侧采集数据，经 RabbitMQ 批量上报后端，并提供 `healthz` 探针。与后端**通过 MQ 解耦**（非直连）。

## 职责

- 从后端 REST `/api/v1/iot/devices` 拉取设备清单（P4-5.1 起不再内置 devices 数组）
- 采集设备数据，按 `batch_size` / `flush_interval_ms` 批量打包
- 经 RabbitMQ `iot.exchange` → `data.report` 路由发布
- 暴露 `healthz` 探针（端口见配置）

## 配置

配置文件：`mes-iot/config/iot.json`

| 字段 | 说明 | 默认值 |
|------|------|--------|
| `amqp_url` | RabbitMQ 连接 | `amqp://mes:mes_dev_pwd@127.0.0.1:5672/` |
| `exchange` | 发布交换机 | `iot.exchange` |
| `routing_key` | 路由键 | `data.report` |
| `batch_size` | 批量大小 | `100` |
| `flush_interval_ms` | 刷新间隔(ms) | `100` |
| `healthz_port` | 探针端口 | `8091` |
| `backend_url` | 后端基址 | `http://127.0.0.1:8088` |
| `backend_user` / `backend_pwd` | 拉取设备清单凭证 | `admin` / 部署注入 |

> 生产环境 `backend_url` 指向 `backend:8088`（Docker 内网），`backend_pwd` 必须由环境变量或挂载配置覆盖，**禁止明文提交**。

## 构建

依赖 vcpkg（同后端，`x64-linux-dynamic` / `x64-windows-mes`），CMake + Ninja：

```bash
cmake -S mes-iot -B mes-iot/build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
cmake --build mes-iot/build -j
```

## 运行

```bash
./mes-iot/build/mes-iot mes-iot/config/iot.json
curl http://localhost:8091/healthz
```

## 测试与压测

- 无需硬件即可用仓库根 `just iot-sim` 启动 IoT 模拟器直发 MQ。
- 端到端见 `tests/e2e/m3_stop_collection_e2e.ps1`（建单→开工→报满→停采链路→幂等→队列隔离→崩溃恢复）。
