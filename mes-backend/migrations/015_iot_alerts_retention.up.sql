-- P2-3.4 iot_alerts 保留策略 (剩余项; 4 个索引已在 010_fixes 补齐)
-- 告警表为普通表(非分区), 用 pg_cron 每日 03:00 清理 180 天前告警。
-- 依赖 idx_alert_created(created_at) 索引 (004 已建), DELETE 走索引扫描。
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_cron') THEN
        -- 幂等: job 已存在则跳过 (重复执行迁移不报错)
        IF NOT EXISTS (SELECT 1 FROM cron.job WHERE jobname = 'iot-alerts-retention') THEN
            PERFORM cron.schedule('iot-alerts-retention', '0 3 * * *',
                $cron$DELETE FROM iot_alerts WHERE created_at < NOW() - INTERVAL '180 days'$cron$);
        END IF;
    ELSE
        RAISE NOTICE 'pg_cron 不可用: iot_alerts 保留策略未启用, 请部署定制镜像';
    END IF;
EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'iot_alerts 保留策略注册失败 (%), 人工执行 DELETE 兜底', SQLERRM;
END $$;
