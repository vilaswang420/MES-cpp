# mes-backend — MES C++ 后端服务

Drogon 框架的异步 C++ 后端，提供 REST API、WebSocket 推送与 RabbitMQ 消费（OEE 计算、停采链路等）。是整个 MES 的**业务与权限核心**。

## 技术要点

- 框架：Drogon（异步、基于协程）
- 数据库：PostgreSQL 16（分区表 + pg_partman）
- 缓存：Redis 7（Cluster 模式）
- 消息队列：RabbitMQ 3.13（AMQP，持久化 + 死信队列）
- 依赖管理：vcpkg（triplet：`x64-linux-dynamic` CI / `x64-windows-mes` 本地 Windows）
- 构建：CMake + Ninja

## 目录结构

```
mes-backend/
├── CMakeLists.txt
├── vcpkg.json
├── config/                 # Drogon 配置变体
│   ├── drogon_config.json        # 开发默认
│   ├── drogon_config.prod.json   # 生产
│   ├── drogon_config.b.json      # blue/green 部署变体
│   ├── drogon_config... (c)      # CI 变体
│   └── rabbitmq.json             # MQ 连接配置
├── src/                    # 控制器 / 服务 / 中间件 / 模型
├── migrations/             # golang-migrate 迁移（17 个 up + 对应 down）
└── tests/                  # 单元测试（mes_unit_tests 目标）
```

## 构建

前置：vcpkg 已安装并设 `VCPKG_ROOT`；CMake ≥ 3.25；编译器 GCC 14 / MSVC 14.44+。

```bash
# 配置（Linux CI 用 x64-linux-dynamic；Windows 本地用 x64-windows-mes）
cmake -S mes-backend -B mes-backend/build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic

# 编译
cmake --build mes-backend/build -j
```

> Windows 本地构建注意：`simpleamqpclient` 仅支持动态链接，必须使用 `x64-windows-mes` overlay triplet；`bcrypt` 走 vendored `bcrypt.c`，不依赖系统 SDK。

## 运行

```bash
# 先确保中间件与迁移就绪（见仓库根 README 的 just dev-up）
./mes-backend/build/mes-backend mes-backend/config/drogon_config.json
# 健康检查
curl http://localhost:8088/healthz
```

- 监听端口：**8088**
- 数据库：`postgres://mes:mes_dev_pwd@localhost:5432/mes`（开发）

## 数据库迁移

使用 [golang-migrate](https://github.com/golang-migrate/migrate)：

```bash
migrate -path mes-backend/migrations \
  -database "postgres://mes:mes_dev_pwd@localhost:5432/mes?sslmode=disable" up
```

- 迁移**只进不退**：生产仅允许 `up`；`down` 仅限开发。破坏性变更按 expand/contract。
- CI 门禁含迁移往返测试（含跨分区插入用例），本地可用 `just migrate-roundtrip` 复现。

## 测试

```bash
ctest --test-dir mes-backend/build --output-on-failure
# 或等价： cmake --build mes-backend/build --target mes_unit_tests && ./mes-backend/build/mes_unit_tests
```

## 关键约定（详见仓库根 README）

- **fail-closed 权限**：所有路由必须在 `src/middlewares/perm_routes.cc` 注册权限映射，CI 强制。
- **MQ 只走 Outbox**：事务内禁止直发 MQ，唯一入口 `OutboxService::enqueue()`（同事务写 `mq_outbox`）。
- **Service 层协程**：事务逻辑用 C++20 协程；回调仅允许在底层插件。
- **统一响应**：`{code,message,data,timestamp,trace_id}`，错误仅由全局拦截器产出。
