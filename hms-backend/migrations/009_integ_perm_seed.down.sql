-- 009_integ_perm_seed.down.sql — 回收集成域增量播种
DELETE FROM integ_api_configs WHERE system_name IN ('ERP 开发桩', 'WMS 开发桩');

DELETE FROM sys_role_permissions WHERE permission_id IN (
    SELECT id FROM sys_permissions WHERE perm_code LIKE 'integ:%');
DELETE FROM sys_permissions WHERE perm_code LIKE 'integ:%';
