-- 001_auth_tables.down.sql (仅 dev 使用; 生产只进不退, 见 CONTRIBUTING.md)
DROP TABLE IF EXISTS sys_configs;
DROP TABLE IF EXISTS mq_outbox;
-- 清理 pg_cron 调度与 partman 注册
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_cron') THEN
        PERFORM cron.unschedule('partman-maintenance-audit');
    END IF;
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_partman') THEN
        DELETE FROM partman.part_config WHERE parent_table = 'public.sys_audit_logs';
    END IF;
EXCEPTION WHEN OTHERS THEN
    NULL;
END $$;
DROP TABLE IF EXISTS sys_audit_logs CASCADE;
DROP TABLE IF EXISTS sys_role_dept_scope;
DROP TABLE IF EXISTS sys_role_permissions;
DROP TABLE IF EXISTS sys_user_roles;
DROP TABLE IF EXISTS sys_permissions;
DROP TABLE IF EXISTS sys_roles;
DROP TABLE IF EXISTS sys_users;
DROP TABLE IF EXISTS sys_departments;
