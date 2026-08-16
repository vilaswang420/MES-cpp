-- 002_seed.up.sql
-- 超管 + 7 默认角色 + 权限树(A/B 域) + 角色授权矩阵
-- 设计事实源: HMS_Architecture_Design.md 4.2-4.6/4.11 节权限码 + 5.5 默认角色矩阵
-- 注意: 权限码必须与 hms-backend/src/middlewares/perm_routes.cc 严格一致 (CI 门禁)

-- ============ 默认部门 ============
INSERT INTO sys_departments (dept_code, dept_name, sort_order)
VALUES ('ROOT', '总公司', 0), ('IT', '信息部', 1)
ON CONFLICT (dept_code) DO NOTHING;

-- ============ 超级管理员 ============
-- 密码: password (由本仓 vendored bcrypt 库生成并往返验证, 仅开发环境! 部署前必须重置)
INSERT INTO sys_users (dept_id, username, password_hash, real_name, employee_no, status)
SELECT d.id, 'admin', '$2a$10$Vq7ZaCglT1G6pU4uG9zUJ.LRt4xCFfHpI1O6xY3FQ/6IxnQHi/JN6',
       '系统管理员', 'E0001', 1
  FROM sys_departments d WHERE d.dept_code = 'IT'
ON CONFLICT (username) DO NOTHING;

-- ============ 7 默认角色 (5.5 节) ============
INSERT INTO sys_roles (role_code, role_name, description, data_scope, sort_order) VALUES
 ('super_admin',  '超级管理员',   '所有权限',                         4, 1),
 ('prod_manager', '生产主管',     '工单管理、排产、报工查看',          3, 2),
 ('operator',     '车间操作员',   '报工、查看工单',                    1, 3),
 ('qc_engineer',  '质量工程师',   '检验录入、缺陷管理',                2, 4),
 ('dev_engineer', '设备工程师',   '设备管理、告警处理',                2, 5),
 ('dashboard',    '看板用户',     '看板查看(只读)',                    4, 6),
 ('api_erp',      'ERP集成',      'ERP/WMS 接口账号',                  1, 7)
ON CONFLICT (role_code) DO NOTHING;

INSERT INTO sys_user_roles (user_id, role_id)
SELECT u.id, r.id FROM sys_users u, sys_roles r
 WHERE u.username = 'admin' AND r.role_code = 'super_admin'
ON CONFLICT DO NOTHING;

-- ============ 权限树 ============
-- 一级菜单
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, icon, sort_order) VALUES
 ('menu:system',     '系统管理', 1, '/system',     'SettingOutlined',    90),
 ('menu:production', '生产管理', 1, '/production', 'ScheduleOutlined',   10),
 ('menu:iot',        '设备物联', 1, '/iot',        'CloudServerOutlined',20),
 ('menu:quality',    '质量管理', 1, '/quality',    'ExperimentOutlined', 30)
ON CONFLICT DO NOTHING;

-- 二级菜单
INSERT INTO sys_permissions (parent_id, perm_code, perm_name, perm_type, path, icon, sort_order)
SELECT p.id, v.code, v.name, 1, v.path, v.icon, v.ord
  FROM sys_permissions p,
       (VALUES
        ('menu:system', 'menu:system:user',   '用户管理', '/system/users',    'UserOutlined',      1),
        ('menu:system', 'menu:system:role',   '角色管理', '/system/roles',    'TeamOutlined',      2),
        ('menu:system', 'menu:system:dept',   '部门管理', '/system/depts',    'ApartmentOutlined', 3),
        ('menu:system', 'menu:system:perm',   '权限管理', '/system/perms',    'SafetyOutlined',    4),
        ('menu:system', 'menu:system:audit',  '审计日志', '/system/audit',    'FileSearchOutlined',5),
        ('menu:system', 'menu:system:config', '系统配置', '/system/configs',  'ToolOutlined',      6),
        ('menu:production', 'menu:prod:wo',    '工单管理', '/production/work-orders', 'ProfileOutlined', 1),
        ('menu:production', 'menu:prod:plan',  '生产计划', '/production/plans',       'CalendarOutlined',2),
        ('menu:production', 'menu:prod:based', '主数据',   '/production/basedata',    'DatabaseOutlined',3),
        ('menu:iot', 'menu:iot:device', '设备管理', '/iot/devices', 'HddOutlined',    1),
        ('menu:iot', 'menu:iot:alert',  '告警中心', '/iot/alerts',  'AlertOutlined',  2),
        ('menu:iot', 'menu:iot:task',   '采集任务', '/iot/tasks',   'FieldTimeOutlined',3),
        ('menu:quality', 'menu:qc:inspection', '检验管理', '/quality/inspections', 'AuditOutlined',   1),
        ('menu:quality', 'menu:qc:defect',     '缺陷管理', '/quality/defects',     'BugOutlined',     2),
        ('menu:quality', 'menu:qc:stat',       '质量统计', '/quality/statistics',  'BarChartOutlined',3)
       ) AS v(parent, code, name, path, icon, ord)
 WHERE p.perm_code = v.parent
ON CONFLICT DO NOTHING;

-- 接口权限 (perm_type=3): 与 perm_routes.cc 一一对应
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, method, status) VALUES
 -- A 域: 用户管理 (4.3)
 ('system:user:list',    '用户列表',   3, '/api/v1/system/users',                     'GET',    1),
 ('system:user:query',   '用户详情',   3, '/api/v1/system/users/{id}',                'GET',    1),
 ('system:user:add',     '新增用户',   3, '/api/v1/system/users',                     'POST',   1),
 ('system:user:update',  '修改用户',   3, '/api/v1/system/users/{id}',                'PUT',    1),
 ('system:user:update',  '启禁用用户', 3, '/api/v1/system/users/{id}/status',         'PUT',    1),
 ('system:user:delete',  '删除用户',   3, '/api/v1/system/users/{id}',                'DELETE', 1),
 ('system:user:reset',   '重置密码',   3, '/api/v1/system/users/{id}/reset-password', 'PUT',    1),
 ('system:user:assign',  '分配角色',   3, '/api/v1/system/users/{id}/roles',          'PUT',    1),
 -- A 域: 角色权限 (4.4)
 ('system:role:list',    '角色列表',   3, '/api/v1/system/roles',                     'GET',    1),
 ('system:role:query',   '角色详情',   3, '/api/v1/system/roles/{id}',                'GET',    1),
 ('system:role:add',     '新增角色',   3, '/api/v1/system/roles',                     'POST',   1),
 ('system:role:update',  '修改角色',   3, '/api/v1/system/roles/{id}',                'PUT',    1),
 ('system:role:update',  '数据范围',   3, '/api/v1/system/roles/{id}/data-scope',     'PUT',    1),
 ('system:role:delete',  '删除角色',   3, '/api/v1/system/roles/{id}',                'DELETE', 1),
 ('system:role:assign',  '分配权限',   3, '/api/v1/system/roles/{id}/permissions',    'PUT',    1),
 ('system:permission:list','权限树',   3, '/api/v1/system/permissions/tree',          'GET',    1),
 -- A 域: 部门 (4.5)
 ('system:dept:list',    '部门树',     3, '/api/v1/system/departments/tree',          'GET',    1),
 ('system:dept:add',     '新增部门',   3, '/api/v1/system/departments',               'POST',   1),
 ('system:dept:update',  '修改部门',   3, '/api/v1/system/departments/{id}',          'PUT',    1),
 ('system:dept:delete',  '删除部门',   3, '/api/v1/system/departments/{id}',          'DELETE', 1),
 -- A 域: 审计与配置 (4.11)
 ('system:audit:list',   '审计查询',   3, '/api/v1/system/audit-logs',                'GET',    1),
 ('system:audit:list',   '审计详情',   3, '/api/v1/system/audit-logs/{id}',           'GET',    1),
 ('system:config:list',  '配置列表',   3, '/api/v1/system/configs',                   'GET',    1),
 ('system:config:update','修改配置',   3, '/api/v1/system/configs/{key}',             'PUT',    1),
 -- B 域: 工单 (4.6)
 ('prod:wo:list',        '工单列表',   3, '/api/v1/production/work-orders',                 'GET',  1),
 ('prod:wo:query',       '工单详情',   3, '/api/v1/production/work-orders/{id}',            'GET',  1),
 ('prod:wo:add',         '新增工单',   3, '/api/v1/production/work-orders',                 'POST', 1),
 ('prod:wo:update',      '修改工单',   3, '/api/v1/production/work-orders/{id}',            'PUT',  1),
 ('prod:wo:schedule',    '排产工单',   3, '/api/v1/production/work-orders/{id}/schedule',   'PUT',  1),
 ('prod:wo:release',     '下达工单',   3, '/api/v1/production/work-orders/{id}/release',    'PUT',  1),
 ('prod:wo:start',       '开始工单',   3, '/api/v1/production/work-orders/{id}/start',      'PUT',  1),
 ('prod:wo:pause',       '暂停工单',   3, '/api/v1/production/work-orders/{id}/pause',      'PUT',  1),
 ('prod:wo:complete',    '完工工单',   3, '/api/v1/production/work-orders/{id}/complete',   'PUT',  1),
 ('prod:wo:close',       '关闭工单',   3, '/api/v1/production/work-orders/{id}/close',      'PUT',  1),
 ('prod:wo:report',      '工序报工',   3, '/api/v1/production/work-orders/{id}/report',     'POST', 1),
 ('prod:wo:cancel',      '取消工单',   3, '/api/v1/production/work-orders/{id}/cancel',     'PUT',  1),
 -- B 域: 产线/工位/工艺/产品/计划 (4.6)
 ('prod:line:list',      '产线列表',   3, '/api/v1/production/lines',                       'GET',  1),
 ('prod:line:add',       '创建产线',   3, '/api/v1/production/lines',                       'POST', 1),
 ('prod:line:edit',      '编辑产线',   3, '/api/v1/production/lines/{id}',                  'PUT',  1),
 ('prod:line:del',       '删除产线',   3, '/api/v1/production/lines/{id}',                  'DELETE', 1),
 ('prod:station:list',   '工位列表',   3, '/api/v1/production/lines/{id}/stations',         'GET',  1),
 ('prod:process:list',   '工艺列表',   3, '/api/v1/production/processes',                   'GET',  1),
 ('prod:process:add',    '创建工艺',   3, '/api/v1/production/processes',                   'POST', 1),
 ('prod:process:edit',   '编辑工艺',   3, '/api/v1/production/processes/{id}',              'PUT',  1),
 ('prod:process:del',    '删除工艺',   3, '/api/v1/production/processes/{id}',              'DELETE', 1),
 ('prod:product:list',   '产品列表',   3, '/api/v1/production/products',                    'GET',  1),
 ('prod:product:add',    '创建产品',   3, '/api/v1/production/products',                    'POST', 1),
 ('prod:product:edit',   '编辑产品',   3, '/api/v1/production/products/{id}',               'PUT',  1),
 ('prod:product:del',    '删除产品',   3, '/api/v1/production/products/{id}',               'DELETE', 1),
 ('prod:plan:list',      '计划列表',   3, '/api/v1/production/plans',                       'GET',  1),
 ('prod:plan:add',       '创建计划',   3, '/api/v1/production/plans',                       'POST', 1),
 -- IoT 域: 设备/传感器/数据/告警/任务 (4.7)
 ('iot:device:list',     '设备列表',   3, '/api/v1/iot/devices',                            'GET',    1),
 ('iot:device:query',    '设备详情',   3, '/api/v1/iot/devices/{id}',                       'GET',    1),
 ('iot:device:query',    '设备状态',   3, '/api/v1/iot/devices/{id}/status',                'GET',    1),
 ('iot:device:add',      '新增设备',   3, '/api/v1/iot/devices',                            'POST',   1),
 ('iot:device:update',   '修改设备',   3, '/api/v1/iot/devices/{id}',                       'PUT',    1),
 ('iot:device:delete',   '删除设备',   3, '/api/v1/iot/devices/{id}',                       'DELETE', 1),
 ('iot:device:command',  '下发指令',   3, '/api/v1/iot/devices/{id}/command',               'POST',   1),
 ('iot:sensor:list',     '传感器列表', 3, '/api/v1/iot/devices/{id}/sensors',               'GET',    1),
 ('iot:sensor:add',      '新增传感器', 3, '/api/v1/iot/devices/{id}/sensors',               'POST',   1),
 ('iot:sensor:update',   '修改传感器', 3, '/api/v1/iot/sensors/{id}',                       'PUT',    1),
 ('iot:sensor:delete',   '删除传感器', 3, '/api/v1/iot/sensors/{id}',                       'DELETE', 1),
 ('iot:data:query',      '实时数据',   3, '/api/v1/iot/devices/{id}/realtime-data',         'GET',    1),
 ('iot:data:query',      '历史数据',   3, '/api/v1/iot/sensors/{id}/history',               'GET',    1),
 ('iot:alert:list',      '告警列表',   3, '/api/v1/iot/alerts',                             'GET',    1),
 ('iot:alert:handle',    '确认告警',   3, '/api/v1/iot/alerts/{id}/acknowledge',            'PUT',    1),
 ('iot:alert:resolve',   '消除告警',   3, '/api/v1/iot/alerts/{id}/resolve',                'PUT',    1),
 ('iot:alert:dismiss',   '忽略告警',   3, '/api/v1/iot/alerts/{id}/dismiss',                'PUT',    1),
 ('iot:task:list',       '采集任务列表',3,'/api/v1/iot/tasks',                              'GET',    1),
 ('iot:task:add',        '新增采集任务',3,'/api/v1/iot/tasks',                              'POST',   1),
 ('iot:task:update',     '修改采集任务',3,'/api/v1/iot/tasks/{id}',                         'PUT',    1),
 ('iot:task:update',     '启停采集任务',3,'/api/v1/iot/tasks/{id}/toggle',                  'PUT',    1),
 ('iot:task:delete',     '删除采集任务',3,'/api/v1/iot/tasks/{id}',                         'DELETE', 1),
 -- 质量域: 检验/缺陷/统计 (4.8)
 ('qc:standard:list',    '检验标准列表',3,'/api/v1/quality/standards',                      'GET',    1),
 ('qc:inspection:add',   '创建检验记录',3,'/api/v1/quality/inspections',                    'POST',   1),
 ('qc:inspection:list',  '检验记录列表',3,'/api/v1/quality/inspections',                    'GET',    1),
 ('qc:inspection:query', '检验详情',   3, '/api/v1/quality/inspections/{id}',               'GET',    1),
 ('qc:defect:list',      '缺陷列表',   3, '/api/v1/quality/defects',                        'GET',    1),
 ('qc:defect:handle',    '缺陷处理',   3, '/api/v1/quality/defects/{id}/disposition',       'PUT',    1),
 ('qc:stat:view',        '质量统计',   3, '/api/v1/quality/statistics',                     'GET',    1),
 -- 集成域: ERP/WMS (4.10)
 ('integ:erp:sync',      'ERP订单同步', 3, '/api/v1/integration/erp/sync-orders',           'POST',   1),
 ('integ:erp:convert',   'ERP订单转工单',3, '/api/v1/integration/erp/{id}/convert',         'POST',   1),
 ('integ:erp:report',    '工单回报ERP', 3, '/api/v1/integration/erp/report',                'POST',   1),
 ('integ:wms:pick',      'WMS领料请求', 3, '/api/v1/integration/wms/pick-request',          'POST',   1),
 ('integ:wms:inbound',   'WMS入库请求', 3, '/api/v1/integration/wms/stock-in',              'POST',   1),
 ('integ:log:list',      '同步日志',   3, '/api/v1/integration/logs',                       'GET',    1),
 ('integ:log:retry',     '重试同步',   3, '/api/v1/integration/logs/{id}/retry',            'POST',   1)
ON CONFLICT DO NOTHING;

-- ============ 授权矩阵 ============
-- super_admin: 全部权限
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r CROSS JOIN sys_permissions p
 WHERE r.role_code = 'super_admin'
ON CONFLICT DO NOTHING;

-- prod_manager: 生产域全部 + 工单操作
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'prod_manager'
   AND p.perm_code IN ('menu:production','menu:prod:wo','menu:prod:plan','menu:prod:based',
       'prod:wo:list','prod:wo:query','prod:wo:add','prod:wo:update','prod:wo:schedule','prod:wo:release',
       'prod:wo:start','prod:wo:pause','prod:wo:complete','prod:wo:close','prod:wo:report','prod:wo:cancel',
       'prod:line:list','prod:line:add','prod:line:edit','prod:line:del','prod:station:list',
       'prod:process:list','prod:process:add','prod:process:edit','prod:process:del',
       'prod:product:list','prod:product:add','prod:product:edit','prod:product:del',
       'prod:plan:list','prod:plan:add')
ON CONFLICT DO NOTHING;

-- operator: 仅本人报工与查看
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'operator'
   AND p.perm_code IN ('menu:production','menu:prod:wo',
       'prod:wo:list','prod:wo:query','prod:wo:report','prod:station:list')
ON CONFLICT DO NOTHING;

-- qc_engineer / dev_engineer: 工单只读 (质检域权限 M2 补充)
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code IN ('qc_engineer','dev_engineer')
   AND p.perm_code IN ('menu:production','menu:prod:wo','prod:wo:list','prod:wo:query')
ON CONFLICT DO NOTHING;

-- dev_engineer: IoT 域全部 (设备管理/告警处理, 5.5 节角色矩阵)
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'dev_engineer'
   AND p.perm_code IN ('menu:iot','menu:iot:device','menu:iot:alert','menu:iot:task',
       'iot:device:list','iot:device:query','iot:device:add','iot:device:update',
       'iot:device:delete','iot:device:command','iot:sensor:list','iot:sensor:add',
       'iot:data:query','iot:alert:list','iot:alert:handle',
       'iot:task:list','iot:task:add','iot:task:update','iot:task:delete')
ON CONFLICT DO NOTHING;

-- qc_engineer: 质量域全部 (检验录入、缺陷管理, 5.5 节角色矩阵)
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'qc_engineer'
   AND p.perm_code IN ('menu:quality','menu:qc:inspection','menu:qc:defect','menu:qc:stat',
       'qc:standard:list','qc:inspection:add','qc:inspection:list','qc:inspection:query',
       'qc:defect:list','qc:defect:handle','qc:stat:view')
ON CONFLICT DO NOTHING;

-- dashboard: 只读
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'dashboard'
   AND p.perm_code IN ('menu:production','menu:prod:wo','prod:wo:list','prod:line:list')
ON CONFLICT DO NOTHING;

-- api_erp: 无前端权限 (M2 集成接口权限另行分配)

-- ============ 默认系统配置 ============
INSERT INTO sys_configs (config_key, config_value, config_type, description, category, is_system) VALUES
 ('auth.jwt.access_ttl_sec',  '7200',  'INT',  'JWT access token 有效期(秒)',   'auth', TRUE),
 ('auth.jwt.refresh_ttl_sec', '604800','INT',  'JWT refresh token 有效期(秒)',  'auth', TRUE),
 ('auth.login.max_fail',      '5',     'INT',  '连续失败锁定阈值',              'auth', TRUE),
 ('sys.timezone',             'UTC',   'STRING','全系统时区约定(只读)',          'sys',  TRUE)
ON CONFLICT (config_key) DO NOTHING;
