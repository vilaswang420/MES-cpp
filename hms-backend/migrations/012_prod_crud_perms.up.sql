-- 012: 生产主数据 CRUD (P1-2.7)
-- 1) 工艺表补软删列 (产线/产品在 003 已有 deleted; 工艺原无, 软删置 deleted=TRUE 且 status=2)
ALTER TABLE prod_processes ADD COLUMN IF NOT EXISTS deleted BOOLEAN DEFAULT FALSE;

-- 2) 计划号全局序列 (createPlan 单号防碰撞, 对齐 prod_work_order_no_seq 方案)
CREATE SEQUENCE IF NOT EXISTS prod_plan_no_seq;

-- 3) 权限种子: 产线/产品/工艺 编辑+删除 (prod_manager 绑定; super_admin 全量自动涵盖;
--    新环境以 002_seed 为权威, 此处 ON CONFLICT DO NOTHING 供已部署环境增量补齐)
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, route_path, method, status) VALUES
('prod:line:edit',    '编辑产线',   3, '/api/v1/production/lines/{id}',      'PUT',    1),
('prod:line:del',     '删除产线',   3, '/api/v1/production/lines/{id}',      'DELETE', 1),
('prod:product:edit', '编辑产品',   3, '/api/v1/production/products/{id}',   'PUT',    1),
('prod:product:del',  '删除产品',   3, '/api/v1/production/products/{id}',   'DELETE', 1),
('prod:process:edit', '编辑工艺',   3, '/api/v1/production/processes/{id}',  'PUT',    1),
('prod:process:del',  '删除工艺',   3, '/api/v1/production/processes/{id}',  'DELETE', 1)
ON CONFLICT (perm_code) DO NOTHING;

INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r, sys_permissions p
 WHERE r.role_code = 'prod_manager' AND p.perm_code IN
       ('prod:line:edit','prod:line:del','prod:product:edit','prod:product:del',
        'prod:process:edit','prod:process:del')
ON CONFLICT DO NOTHING;
