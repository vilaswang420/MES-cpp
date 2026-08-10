-- 容器首次初始化: 创建扩展 (迁移脚本中的 partman/cron 注册依赖这些扩展存在)
-- pg_cron 由 CMD 的 shared_preload_libraries 加载后方可 CREATE EXTENSION
CREATE EXTENSION IF NOT EXISTS pg_partman SCHEMA partman;
CREATE EXTENSION IF NOT EXISTS pg_cron;
