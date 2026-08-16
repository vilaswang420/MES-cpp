-- P2-2.11h 回退: 移除工序 scrap 列
ALTER TABLE prod_work_order_operations
    DROP COLUMN IF EXISTS scrap_qty;
