-- 002_seed.down.sql (仅 dev 使用)
DELETE FROM sys_configs WHERE config_key IN
 ('auth.jwt.access_ttl_sec','auth.jwt.refresh_ttl_sec','auth.login.max_fail','sys.timezone');
DELETE FROM sys_role_permissions;
DELETE FROM sys_user_roles;
DELETE FROM sys_permissions;
DELETE FROM sys_roles;
DELETE FROM sys_users WHERE username = 'admin';
DELETE FROM sys_departments WHERE dept_code IN ('ROOT','IT');
