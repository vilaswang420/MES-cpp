-- 008_qc_perm_seed.down.sql
DELETE FROM sys_role_permissions WHERE permission_id IN (
    SELECT id FROM sys_permissions
     WHERE perm_code LIKE 'qc:%' OR perm_code LIKE 'menu:qc%' OR perm_code = 'menu:quality');
DELETE FROM sys_permissions
 WHERE perm_code LIKE 'qc:%' OR perm_code LIKE 'menu:qc%' OR perm_code = 'menu:quality';
