-- 016_iot_mgmt_perms.up.sql — P4-5.7 IoT 管理补齐
-- 1) iot_sensors 软删列 (deleted): 传感器删除改为软删, 保留历史数据 FK 完整性
-- 2) 新增 4 条接口权限: 告警 resolve/dismiss + 传感器 update/delete
-- 权限码与 hms-backend/src/middlewares/perm_routes.cc 严格一致 (CI 门禁)。

-- 1) 传感器软删
ALTER TABLE iot_sensors ADD COLUMN IF NOT EXISTS deleted BOOLEAN NOT NULL DEFAULT FALSE;
CREATE INDEX IF NOT EXISTS idx_sensor_device_active ON iot_sensors(device_id) WHERE deleted = FALSE;

-- 2) 接口权限 (perm_type=3)
INSERT INTO sys_permissions (perm_code, perm_name, perm_type, path, method, status) VALUES
 ('iot:sensor:update', '修改传感器', 3, '/api/v1/iot/sensors/{id}',            'PUT',    1),
 ('iot:sensor:delete', '删除传感器', 3, '/api/v1/iot/sensors/{id}',            'DELETE', 1),
 ('iot:alert:resolve', '消除告警',   3, '/api/v1/iot/alerts/{id}/resolve',     'PUT',    1),
 ('iot:alert:dismiss', '忽略告警',   3, '/api/v1/iot/alerts/{id}/dismiss',     'PUT',    1)
ON CONFLICT DO NOTHING;

-- super_admin: 新增权限全授权
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r JOIN sys_permissions p
  ON p.perm_code IN ('iot:sensor:update','iot:sensor:delete',
                     'iot:alert:resolve','iot:alert:dismiss')
WHERE r.role_code = 'super_admin'
ON CONFLICT DO NOTHING;

-- dev_engineer: IoT 域全部 (5.5 节角色矩阵)
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r JOIN sys_permissions p
  ON p.perm_code IN ('iot:sensor:update','iot:sensor:delete',
                     'iot:alert:resolve','iot:alert:dismiss')
WHERE r.role_code = 'dev_engineer'
ON CONFLICT DO NOTHING;

-- operator: 告警处置 (确认/消除/忽略), 不含传感器管理
INSERT INTO sys_role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM sys_roles r JOIN sys_permissions p
  ON p.perm_code IN ('iot:alert:resolve','iot:alert:dismiss')
WHERE r.role_code = 'operator'
ON CONFLICT DO NOTHING;
