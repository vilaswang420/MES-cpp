-- 007_iot_perm_seed.up.sql — IoT 域权限播种 (增量, 与 002_seed 中 IoT 段一致)
-- 背景: 002 已应用后新增 IoT 18 接口 (设计文档 4.7), 对存量库增量补权限与授权。
-- 权限码与 hms-backend/src/middlewares/perm_routes.cc 严格一致 (CI 门禁)。

-- 一级菜单
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, icon, sort_order) VALUES
 ('menu:iot', '设备物联', 1, '/iot', 'CloudServerOutlined', 20)
ON CONFLICT DO NOTHING;

-- 二级菜单
INSERT INTO sys_permissions (parent_id, perm_code, perm_name, perm_type, path, icon, sort_order)
SELECT p.id, v.code, v.name, 1, v.path, v.icon, v.ord
  FROM sys_permissions p,
       (VALUES
        ('menu:iot', 'menu:iot:device', '设备管理', '/iot/devices', 'HddOutlined',    1),
        ('menu:iot', 'menu:iot:alert',  '告警中心', '/iot/alerts',  'AlertOutlined',  2),
        ('menu:iot', 'menu:iot:task',   '采集任务', '/iot/tasks',   'FieldTimeOutlined',3)
       ) AS v(parent, code, name, path, icon, ord)
 WHERE p.perm_code = v.parent
ON CONFLICT DO NOTHING;

-- 接口权限 (perm_type=3): 与 perm_routes.cc 一一对应
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, method, status) VALUES
 ('iot:device:list',     '设备列表',   3, '/api/v1/iot/devices',                    'GET',    1),
 ('iot:device:query',    '设备详情',   3, '/api/v1/iot/devices/{id}',               'GET',    1),
 ('iot:device:query',    '设备状态',   3, '/api/v1/iot/devices/{id}/status',        'GET',    1),
 ('iot:device:add',      '新增设备',   3, '/api/v1/iot/devices',                    'POST',   1),
 ('iot:device:update',   '修改设备',   3, '/api/v1/iot/devices/{id}',               'PUT',    1),
 ('iot:device:delete',   '删除设备',   3, '/api/v1/iot/devices/{id}',               'DELETE', 1),
 ('iot:device:command',  '下发指令',   3, '/api/v1/iot/devices/{id}/command',       'POST',   1),
 ('iot:sensor:list',     '传感器列表', 3, '/api/v1/iot/devices/{id}/sensors',       'GET',    1),
 ('iot:sensor:add',      '新增传感器', 3, '/api/v1/iot/devices/{id}/sensors',       'POST',   1),
 ('iot:data:query',      '实时数据',   3, '/api/v1/iot/devices/{id}/realtime-data', 'GET',    1),
 ('iot:data:query',      '历史数据',   3, '/api/v1/iot/sensors/{id}/history',       'GET',    1),
 ('iot:alert:list',      '告警列表',   3, '/api/v1/iot/alerts',                     'GET',    1),
 ('iot:alert:handle',    '确认告警',   3, '/api/v1/iot/alerts/{id}/acknowledge',    'PUT',    1),
 ('iot:task:list',       '采集任务列表',3, '/api/v1/iot/tasks',                     'GET',    1),
 ('iot:task:add',        '新增采集任务',3, '/api/v1/iot/tasks',                     'POST',   1),
 ('iot:task:update',     '修改采集任务',3, '/api/v1/iot/tasks/{id}',                'PUT',    1),
 ('iot:task:update',     '启停采集任务',3, '/api/v1/iot/tasks/{id}/toggle',         'PUT',    1),
 ('iot:task:delete',     '删除采集任务',3, '/api/v1/iot/tasks/{id}',                'DELETE', 1)
ON CONFLICT DO NOTHING;

-- super_admin: 全部新增权限
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'super_admin' AND p.perm_code LIKE 'iot:%' OR
       (r.role_code = 'super_admin' AND p.perm_code LIKE 'menu:iot%')
ON CONFLICT DO NOTHING;

-- dev_engineer: IoT 域全部 (5.5 节角色矩阵)
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'dev_engineer'
   AND p.perm_code IN ('menu:iot','menu:iot:device','menu:iot:alert','menu:iot:task',
       'iot:device:list','iot:device:query','iot:device:add','iot:device:update',
       'iot:device:delete','iot:device:command','iot:sensor:list','iot:sensor:add',
       'iot:data:query','iot:alert:list','iot:alert:handle',
       'iot:task:list','iot:task:add','iot:task:update','iot:task:delete')
ON CONFLICT DO NOTHING;
