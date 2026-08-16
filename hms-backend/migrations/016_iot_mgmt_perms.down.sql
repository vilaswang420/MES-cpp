-- 016_iot_mgmt_perms.down.sql — 回滚 P4-5.7

-- 角色授权回收
DELETE FROM sys_role_permissions rp
 USING sys_permissions p
 WHERE rp.permission_id = p.id
   AND p.perm_code IN ('iot:sensor:update','iot:sensor:delete',
                       'iot:alert:resolve','iot:alert:dismiss');

-- 权限删除
DELETE FROM sys_permissions
 WHERE perm_code IN ('iot:sensor:update','iot:sensor:delete',
                     'iot:alert:resolve','iot:alert:dismiss');

-- 软删列回滚 (已软删的传感器恢复可见)
DROP INDEX IF EXISTS idx_sensor_device_active;
ALTER TABLE iot_sensors DROP COLUMN IF EXISTS deleted;
