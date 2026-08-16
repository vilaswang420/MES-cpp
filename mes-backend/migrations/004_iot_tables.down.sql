-- 004_iot_tables.down.sql (仅 dev 使用)
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_cron') THEN
        PERFORM cron.unschedule('partman-maintenance-iot');
    END IF;
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_partman') THEN
        DELETE FROM partman.part_config WHERE parent_table = 'public.iot_raw_data';
    END IF;
EXCEPTION WHEN OTHERS THEN
    NULL;
END $$;
DROP TABLE IF EXISTS iot_task_devices;
DROP TABLE IF EXISTS iot_collection_tasks;
DROP TABLE IF EXISTS iot_alerts;
DROP TABLE IF EXISTS iot_raw_data CASCADE;
DROP TABLE IF EXISTS iot_sensors;
DROP TABLE IF EXISTS iot_devices;
DROP TABLE IF EXISTS iot_device_types;
