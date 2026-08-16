-- P1-2.4 报工质检门禁灰度开关 (默认开启, 可回退)
-- 需质检工序 (prod_process_steps.quality_check=true) 报工前须已有
-- 合格/让步检验记录 (qc_inspections.result IN (1,3)), 否则 report 返回 409。
INSERT INTO sys_configs (config_key, config_value, config_type, description, category, is_system)
VALUES ('quality_gate_enabled', 'true', 'BOOL',
        '报工质检门禁: 需质检工序无合格/让步检验记录时拒绝报工', 'quality', TRUE)
ON CONFLICT (config_key) DO UPDATE
    SET config_value = EXCLUDED.config_value,
        description  = EXCLUDED.description,
        updated_at   = NOW();
