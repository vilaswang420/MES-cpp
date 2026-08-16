-- 011: 回滚

DELETE FROM sys_role_permissions
WHERE permission_id IN (SELECT id FROM sys_permissions WHERE perm_code = 'prod:wo:cancel');

DELETE FROM sys_permissions WHERE perm_code = 'prod:wo:cancel';
