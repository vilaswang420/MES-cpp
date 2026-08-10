# 贡献指南

## 分支与提交

- 主干 `main` 永远可发布; 功能分支 `feat/<domain>-<slug>`, 修复 `fix/<slug>`。
- 每个 PR 必须通过 CI 全部门禁 (编译、单测、clang-format、migrate 往返、权限映射检查、契约 schema 校验)。

## 数据库迁移规范 (expand/contract)

生产环境迁移**只进不退**。任何破坏性 schema 变更必须拆成三步:

1. **expand**: 新增列/表/索引 (向后兼容, 老代码可继续运行);
2. **migrate**: 应用层双写/回填数据, 全部读路径切换到新结构;
3. **contract**: 确认旧结构无流量后, 新迁移删除旧列/表。

- down 脚本仅用于本地开发回退, 不得包含生产数据处理逻辑;
- 分区表 (sys_audit_logs / iot_raw_data) 的结构变更需同时验证 pg_partman 注册不受影响;
- 每次新增分区表或分区策略变更, 必须在 `scripts/test-migrate-roundtrip.ps1` 中补充跨分区插入用例。

## 路由与权限 (fail-closed)

1. 新增 Controller 路由后, **必须**在 `hms-backend/src/middlewares/perm_routes.cc` 注册 `(path_pattern, method) -> perm_code` 映射, 并在 `002_seed` 或新迁移中补权限记录;
2. `scripts/check_perm_mapping.py` 会扫描所有 `ADD_METHOD_TO` 声明与 perm_routes 注册表, 缺失即构建失败;
3. 公开接口 (login/captcha/healthz) 必须显式列入白名单, 不允许"恰好没配权限"式放行。

## MQ 使用规范

1. 业务事务内**禁止**直接 publish MQ; 唯一入口 `OutboxService::enqueue()` (同事务写 `mq_outbox`, 提交后由投递器发送);
2. 消息体必须符合 `contracts/` 下对应 JSON Schema, 必含 `version` 字段;
3. 消费者必须处理 `x-retry-count` 头: 超过阈值 nack 进 DLQ, 禁止无限重投;
4. 消费者必须幂等 (业务唯一键或去重表)。

## 代码风格

- C++20, clang-format (配置见 `hms-backend/.clang-format`); Service 层用 Drogon 协程, 回调只留底层插件;
- 时间一律 UTC ISO8601 带 `Z`; 统一响应 `{code,message,data,timestamp,trace_id}`;
- 错误 JSON 只允许全局错误拦截器 (`registerHandlingErrorAdvice`) 产出, 业务代码抛 `ApiException`。
