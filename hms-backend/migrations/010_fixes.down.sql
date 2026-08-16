-- 010: 回滚 (与 up 严格对称)

DROP INDEX IF EXISTS idx_outbox_status_retry;
DROP INDEX IF EXISTS idx_audit_module;
DROP INDEX IF EXISTS idx_insp_inspected_at;
DROP INDEX IF EXISTS idx_wo_created_by;

COMMENT ON COLUMN mq_outbox.status IS '0:待投递 1:已投递 2:失败';

ALTER TABLE qc_defects
    DROP COLUMN IF EXISTS disposition_at,
    DROP COLUMN IF EXISTS disposition_by;
