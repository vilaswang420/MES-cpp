-- 容器首次初始化: 创建扩展 (迁移脚本中的 partman/cron 注册依赖这些扩展存在)
-- pg_cron 由 CMD 的 shared_preload_libraries 加载后方可 CREATE EXTENSION
-- 注意: pg_partman 必须装在 partman schema (迁移脚本用 partman.create_parent 等),
-- 而 CREATE EXTENSION ... SCHEMA partman 要求目标 schema 已存在, 故先建 schema。
CREATE SCHEMA IF NOT EXISTS partman;
CREATE EXTENSION IF NOT EXISTS pg_partman SCHEMA partman;
CREATE EXTENSION IF NOT EXISTS pg_cron;
