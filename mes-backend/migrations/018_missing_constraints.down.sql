-- 018 回滚: P2-3.5 缺失约束补齐

ALTER TABLE integ_erp_orders DROP CONSTRAINT IF EXISTS fk_erp_wo;
ALTER TABLE qc_defects DROP CONSTRAINT IF EXISTS fk_defect_operator;
ALTER TABLE qc_defects DROP CONSTRAINT IF EXISTS fk_defect_station;
DROP INDEX IF EXISTS uq_wo_erp_no;
