-- 002_seed.up.sql
-- 超管 + 7 默认角色 + 权限树(A/B 域) + 角色授权矩阵
-- 设计事实源: HMS_Architecture_Design.md 4.2-4.6/4.11 节权限码 + 5.5 默认角色矩阵
-- 注意: 权限码必须与 hms-backend/src/middlewares/perm_routes.cc 严格一致 (CI 门禁)

-- ============ 默认部门 ============
INSERT INTO sys_departments (dept_code, dept_name, sort_order)
VALUES ('ROOT', '总公司', 0), ('IT', '信息部', 1)
ON CONFLICT (dept_code) DO NOTHING;

-- ============ 超级管理员 ============
-- 密码: password (bcrypt 测试向量, 仅开发环境! 部署前必须重置)
INSERT INTO sys_users (dept_id, username, password_hash, real_name, employee_no, status)
SELECT d.id, 'admin', '$2b$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy',
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
 ('menu:production', '生产管理', 1, '/production', 'ScheduleOutlined',   10)
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
        ('menu:production', 'menu:prod:based', '主数据',   '/production/basedata',    'DatabaseOutlined',3)
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
 -- B 域: 产线/工位/工艺/产品/计划 (4.6)
 ('prod:line:list',      '产线列表',   3, '/api/v1/production/lines',                       'GET',  1),
 ('prod:line:add',       '创建产线',   3, '/api/v1/production/lines',                       'POST', 1),
 ('prod:station:list',   '工位列表',   3, '/api/v1/production/lines/{id}/stations',         'GET',  1),
 ('prod:process:list',   '工艺列表',   3, '/api/v1/production/processes',                   'GET',  1),
 ('prod:process:add',    '创建工艺',   3, '/api/v1/production/processes',                   'POST', 1),
 ('prod:product:list',   '产品列表',   3, '/api/v1/production/products',                    'GET',  1),
 ('prod:product:add',    '创建产品',   3, '/api/v1/production/products',                    'POST', 1),
 ('prod:plan:list',      '计划列表',   3, '/api/v1/production/plans',                       'GET',  1),
 ('prod:plan:add',       '创建计划',   3, '/api/v1/production/plans',                       'POST', 1)
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
       'prod:wo:start','prod:wo:pause','prod:wo:complete','prod:wo:close','prod:wo:report',
       'prod:line:list','prod:line:add','prod:station:list','prod:process:list',
       'prod:process:add','prod:product:list','prod:product:add',
       'prod:plan:list','prod:plan:add')
ON CONFLICT DO NOTHING;

-- operator: 仅本人报工与查看
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'operator'
   AND p.perm_code IN ('menu:production','menu:prod:wo',
       'prod:wo:list','prod:wo:query','prod:wo:report','prod:station:list')
ON CONFLICT DO NOTHING;

-- qc_engineer / dev_engineer: 工单只读 (质检/设备域权限 M2 补充)
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code IN ('qc_engineer','dev_engineer')
   AND p.perm_code IN ('menu:production','menu:prod:wo','prod:wo:list','prod:wo:query')
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
