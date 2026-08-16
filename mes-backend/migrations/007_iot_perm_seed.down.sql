-- 007_iot_perm_seed.down.sql
DELETE FROM sys_role_permissions WHERE permission_id IN (
    SELECT id FROM sys_permissions WHERE perm_code LIKE 'iot:%' OR perm_code LIKE 'menu:iot%');
DELETE FROM sys_permissions WHERE perm_code LIKE 'iot:%' OR perm_code LIKE 'menu:iot%';
