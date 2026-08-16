-- 012 down: 与 up 严格对称
DELETE FROM sys_role_permissions WHERE permission_id IN (
    SELECT id FROM sys_permissions
     WHERE perm_code IN ('prod:line:edit','prod:line:del','prod:product:edit',
                         'prod:product:del','prod:process:edit','prod:process:del'));
DELETE FROM sys_permissions
 WHERE perm_code IN ('prod:line:edit','prod:line:del','prod:product:edit',
                     'prod:product:del','prod:process:edit','prod:process:del');

DROP SEQUENCE IF EXISTS prod_plan_no_seq;

ALTER TABLE prod_processes DROP COLUMN IF EXISTS deleted;
