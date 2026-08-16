-- P1-2.4 回退: 删除报工质检门禁开关
DELETE FROM sys_configs WHERE config_key = 'quality_gate_enabled';
