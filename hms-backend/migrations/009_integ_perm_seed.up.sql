-- 009_integ_perm_seed.up.sql — 集成域增量播种 (权限 + ERP/WMS 接入配置)
-- 背景: 002 已应用的存量库增量补 4.10 节 7 接口权限与 integ_api_configs 种子。
-- 权限码与 hms-backend/src/middlewares/perm_routes.cc 严格一致 (CI 门禁)。

-- 接口权限 (perm_type=3)
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, method, status) VALUES
 ('integ:erp:sync',    'ERP订单同步',   3, '/api/v1/integration/erp/sync-orders',      'POST', 1),
 ('integ:erp:convert', 'ERP订单转工单', 3, '/api/v1/integration/erp/{id}/convert',     'POST', 1),
 ('integ:erp:report',  '工单回报ERP',   3, '/api/v1/integration/erp/report',           'POST', 1),
 ('integ:wms:pick',    'WMS领料请求',   3, '/api/v1/integration/wms/pick-request',     'POST', 1),
 ('integ:wms:inbound', 'WMS入库请求',   3, '/api/v1/integration/wms/stock-in',         'POST', 1),
 ('integ:log:list',    '同步日志',      3, '/api/v1/integration/logs',                 'GET',  1),
 ('integ:log:retry',   '重试同步',      3, '/api/v1/integration/logs/{id}/retry',      'POST', 1)
ON CONFLICT DO NOTHING;

-- super_admin: 集成域全部
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'super_admin' AND p.perm_code LIKE 'integ:%'
ON CONFLICT DO NOTHING;

-- dev_engineer: 集成域全部 (5.5 节角色矩阵: 集成对接归开发/实施)
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'dev_engineer' AND p.perm_code LIKE 'integ:%'
ON CONFLICT DO NOTHING;

-- ERP/WMS 接入配置种子: dev 环境指向本地桩 (scripts/erp_wms_stub.py, 端口 9095)。
-- 生产经部署配置覆盖 base_url/token_key。
INSERT INTO integ_api_configs (system_type, system_name, base_url, auth_type, timeout_ms, retry_count, enabled)
VALUES
 ('ERP', 'ERP 开发桩', 'http://127.0.0.1:9095', 'Bearer', 10000, 3, TRUE),
 ('WMS', 'WMS 开发桩', 'http://127.0.0.1:9095', 'Bearer', 10000, 3, TRUE)
ON CONFLICT DO NOTHING;
