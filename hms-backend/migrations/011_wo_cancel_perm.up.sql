-- 011: 工单取消权限 (P1-2.6 cancel 路由)
-- 新增 sys_permissions 记录 + 绑定 prod_manager 角色 (super_admin 全量自动涵盖)

INSERT INTO sys_permissions (perm_code, perm_name, perm_type, route_path, method, status)
VALUES ('prod:wo:cancel', '取消工单', 3, '/api/v1/production/work-orders/{id}/cancel', 'PUT', 1)
ON CONFLICT (perm_code) DO NOTHING;

INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'prod_manager' AND p.perm_code = 'prod:wo:cancel'
ON CONFLICT DO NOTHING;
