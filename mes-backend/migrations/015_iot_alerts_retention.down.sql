-- P2-3.4 回退: 移除 iot_alerts 180 天保留任务
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_cron') THEN
        PERFORM cron.unschedule('iot-alerts-retention');
    END IF;
END $$;
