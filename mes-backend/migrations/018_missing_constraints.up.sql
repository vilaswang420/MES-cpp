-- 018: P2-3.5 缺失约束补齐 (2026-08-20)
-- 来源: GAP_ANALYSIS.md §4.2。仅在受控/干净库执行;
--       若已存在违反数据, FK 添加会被跳过并 RAISE NOTICE, 不阻断迁移。
-- 策略: FK 用 ON DELETE SET NULL —— 防止删除被引用行时产生孤立记录, 但不阻断删除。
--       所有改动幂等 (IF NOT EXISTS / 存在性检查), 可重复执行迁移。

-- ============ 1. prod_work_orders.erp_order_no 唯一 (防重复关联 ERP 订单) ============
-- 列允许 NULL (003:87 无 NOT NULL), 用部分唯一索引: 仅约束非 NULL 值, 允许多个 NULL。
CREATE UNIQUE INDEX IF NOT EXISTS uq_wo_erp_no
    ON prod_work_orders(erp_order_no) WHERE erp_order_no IS NOT NULL;

-- ============ 2. integ_erp_orders.work_order_id -> prod_work_orders(id) ============
DO $$
DECLARE
    bad int;
BEGIN
    SELECT count(*) INTO bad
    FROM integ_erp_orders e
    WHERE e.work_order_id IS NOT NULL
      AND NOT EXISTS (SELECT 1 FROM prod_work_orders w WHERE w.id = e.work_order_id);
    IF bad > 0 THEN
        RAISE NOTICE 'integ_erp_orders 存在 % 条 work_order_id 孤立记录, 跳过 FK 添加', bad;
        RETURN;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'fk_erp_wo') THEN
        ALTER TABLE integ_erp_orders
            ADD CONSTRAINT fk_erp_wo
            FOREIGN KEY (work_order_id) REFERENCES prod_work_orders(id) ON DELETE SET NULL;
    END IF;
END $$;

-- ============ 3. qc_defects.operator_id -> sys_users(id) ============
DO $$
DECLARE
    bad int;
BEGIN
    SELECT count(*) INTO bad
    FROM qc_defects d
    WHERE d.operator_id IS NOT NULL
      AND NOT EXISTS (SELECT 1 FROM sys_users u WHERE u.id = d.operator_id);
    IF bad > 0 THEN
        RAISE NOTICE 'qc_defects 存在 % 条 operator_id 孤立记录, 跳过 FK 添加', bad;
        RETURN;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'fk_defect_operator') THEN
        ALTER TABLE qc_defects
            ADD CONSTRAINT fk_defect_operator
            FOREIGN KEY (operator_id) REFERENCES sys_users(id) ON DELETE SET NULL;
    END IF;
END $$;

-- ============ 4. qc_defects.station_id -> prod_workstations(id) ============
DO $$
DECLARE
    bad int;
BEGIN
    SELECT count(*) INTO bad
    FROM qc_defects d
    WHERE d.station_id IS NOT NULL
      AND NOT EXISTS (SELECT 1 FROM prod_workstations s WHERE s.id = d.station_id);
    IF bad > 0 THEN
        RAISE NOTICE 'qc_defects 存在 % 条 station_id 孤立记录, 跳过 FK 添加', bad;
        RETURN;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'fk_defect_station') THEN
        ALTER TABLE qc_defects
            ADD CONSTRAINT fk_defect_station
            FOREIGN KEY (station_id) REFERENCES prod_workstations(id) ON DELETE SET NULL;
    END IF;
END $$;
