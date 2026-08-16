-- P2-2.11h 工序级 scrap 记录
-- 报工传 scrap_qty 时除累计工单 prod_work_orders.scrap_qty 外,
-- 同步累计工序 prod_work_order_operations.scrap_qty (此前该列缺失,
-- 工序级只记录了 good/defect, scrap 仅体现在工单汇总)。
ALTER TABLE prod_work_order_operations
    ADD COLUMN scrap_qty INT DEFAULT 0;
COMMENT ON COLUMN prod_work_order_operations.scrap_qty IS '工序累计报废数 (P2-2.11h)';
