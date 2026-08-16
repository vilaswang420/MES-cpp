-- 010: P1 正确性修复 + P2 索引补齐 (2026-08-16, CORE_PLAN v1.1)
-- 内容:
--   1. qc_defects 加处置人/处置时间 (P1-2.5 缺陷处置记录处置人)
--   2. mq_outbox.status 注释扩展死信语义 (P2-3.3, 值 3 由 OutboxDispatcher 写入)
--   3. 4 个缺失索引 (P2-3.4)

-- ============ 1. qc_defects 处置人/处置时间 (P1-2.5) ============
ALTER TABLE qc_defects
    ADD COLUMN disposition_by BIGINT,   -- 处置人 (sys_users.id), 解决 (void)userId 丢弃问题
    ADD COLUMN disposition_at TIMESTAMPTZ; -- 处置时间

-- ============ 2. mq_outbox 死信状态注释 (P2-3.3) ============
COMMENT ON COLUMN mq_outbox.status IS '0:待投递 1:已投递 2:失败 3:死信(重试超限, 人工介入)';

-- ============ 3. 缺失索引 (P2-3.4) ============
-- 工单按创建人过滤 (data_scope 档位 1/2 下钻)
CREATE INDEX IF NOT EXISTS idx_wo_created_by ON prod_work_orders(created_by);
-- 检验记录按时间统计 (质量趋势查询)
CREATE INDEX IF NOT EXISTS idx_insp_inspected_at ON qc_inspections(inspected_at);
-- 审计日志按模块过滤 (合规审计)
CREATE INDEX IF NOT EXISTS idx_audit_module ON sys_audit_logs(module);
-- outbox 按状态+重试次数扫描 (死信判定/重试队列)
CREATE INDEX IF NOT EXISTS idx_outbox_status_retry ON mq_outbox(status, retry_count);
