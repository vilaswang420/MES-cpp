-- 003_prod_tables.down.sql (仅 dev 使用)
DROP TABLE IF EXISTS prod_plan_work_orders;
DROP TABLE IF EXISTS prod_production_plans;
DROP TABLE IF EXISTS prod_work_order_operations;
DROP TABLE IF EXISTS prod_work_orders;
ALTER TABLE prod_products DROP CONSTRAINT IF EXISTS fk_product_process;
DROP TABLE IF EXISTS prod_process_steps;
DROP TABLE IF EXISTS prod_processes;
DROP TABLE IF EXISTS prod_workstations;
DROP TABLE IF EXISTS prod_production_lines;
DROP TABLE IF EXISTS prod_products;
