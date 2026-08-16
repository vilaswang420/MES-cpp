# spike/ — 一次性 POC (不进主干)

本目录存放阶段 S0 的预研验证代码, 结论沉淀到 `docs/adr/`, 代码本身不作为交付物。

| POC | 目标 | 产出 |
|-----|------|------|
| `drogon-infra-poc/` | Drogon + PG16 读写 + Redis pub/sub + rabbitmq-c 收发全链路, 锁定各库版本 | 版本清单 + compose 全链路跑通记录 |
| `authforge-poc/` | AuthForge `find_package` 构建兼容性 / identity 库抽取 / schema 隔离 (限时 3 天) | Go/No-Go → `docs/adr/0001-authforge-integration.md` (已定稿: No-Go) |

约定:

- 每个 POC 自带独立 README 与最小 compose/构建说明, 可单独跑通;
- POC 依赖版本一旦锁定, 同步写入 `mes-backend/vcpkg.json`, 之后本目录代码冻结;
- 禁止从主干反向引用本目录任何代码。
