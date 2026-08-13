-- 008_qc_perm_seed.up.sql — 质量域权限播种 (增量, 与 002_seed 中质量段一致)
-- 背景: 002 已应用后新增质量域 7 接口 (设计文档 4.8), 对存量库增量补权限与授权。
-- 权限码与 hms-backend/src/middlewares/perm_routes.cc 严格一致 (CI 门禁)。

-- 一级菜单
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, icon, sort_order) VALUES
 ('menu:quality', '质量管理', 1, '/quality', 'ExperimentOutlined', 30)
ON CONFLICT DO NOTHING;

-- 二级菜单
INSERT INTO sys_permissions (parent_id, perm_code, perm_name, perm_type, path, icon, sort_order)
SELECT p.id, v.code, v.name, 1, v.path, v.icon, v.ord
  FROM sys_permissions p,
       (VALUES
        ('menu:quality', 'menu:qc:inspection', '检验管理', '/quality/inspections', 'AuditOutlined',   1),
        ('menu:quality', 'menu:qc:defect',     '缺陷管理', '/quality/defects',     'BugOutlined',     2),
        ('menu:quality', 'menu:qc:stat',       '质量统计', '/quality/statistics',  'BarChartOutlined',3)
       ) AS v(parent, code, name, path, icon, ord)
 WHERE p.perm_code = v.parent
ON CONFLICT DO NOTHING;

-- 接口权限 (perm_type=3): 与 perm_routes.cc 一一对应
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, method, status) VALUES
 ('qc:standard:list',    '检验标准列表',3,'/api/v1/quality/standards',                      'GET',    1),
 ('qc:inspection:add',   '创建检验记录',3,'/api/v1/quality/inspections',                    'POST',   1),
 ('qc:inspection:list',  '检验记录列表',3,'/api/v1/quality/inspections',                    'GET',    1),
 ('qc:inspection:query', '检验详情',   3, '/api/v1/quality/inspections/{id}',               'GET',    1),
 ('qc:defect:list',      '缺陷列表',   3, '/api/v1/quality/defects',                        'GET',    1),
 ('qc:defect:handle',    '缺陷处理',   3, '/api/v1/quality/defects/{id}/disposition',       'PUT',    1),
 ('qc:stat:view',        '质量统计',   3, '/api/v1/quality/statistics',                     'GET',    1)
ON CONFLICT DO NOTHING;

-- super_admin: 全部新增权限
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'super_admin'
   AND (p.perm_code LIKE 'qc:%' OR p.perm_code LIKE 'menu:qc%' OR p.perm_code = 'menu:quality')
ON CONFLICT DO NOTHING;

-- qc_engineer: 质量域全部 (5.5 节角色矩阵)
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'qc_engineer'
   AND p.perm_code IN ('menu:quality','menu:qc:inspection','menu:qc:defect','menu:qc:stat',
       'qc:standard:list','qc:inspection:add','qc:inspection:list','qc:inspection:query',
       'qc:defect:list','qc:defect:handle','qc:stat:view')
ON CONFLICT DO NOTHING;
