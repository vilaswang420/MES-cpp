-- 004_iot_tables.up.sql — IoT 数据采集 7 表
-- 设计事实源: HMS_Architecture_Design.md 3.4 节

CREATE TABLE iot_device_types (
    id              BIGSERIAL PRIMARY KEY,
    type_code       VARCHAR(64) NOT NULL UNIQUE,
    type_name       VARCHAR(128) NOT NULL,
    manufacturer    VARCHAR(128),
    protocol        VARCHAR(32),        -- OPC-UA / Modbus / MQTT
    config_template JSONB,              -- 采集配置模板
    status          SMALLINT DEFAULT 1,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE iot_devices (
    id              BIGSERIAL PRIMARY KEY,
    device_code     VARCHAR(64) NOT NULL UNIQUE,
    device_name     VARCHAR(128) NOT NULL,
    type_id         BIGINT REFERENCES iot_device_types(id),
    line_id         BIGINT REFERENCES prod_production_lines(id),
    workstation_id  BIGINT REFERENCES prod_workstations(id),
    ip_address      INET,
    port            INT,
    protocol        VARCHAR(32) NOT NULL,
    connection_config JSONB,            -- 连接参数 (Modbus地址/MQTT Topic等)
    status          SMALLINT DEFAULT 0,
    -- 0:离线 1:在线 2:故障 3:维护
    last_heartbeat_at TIMESTAMPTZ,
    installed_at    DATE,
    deleted         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_device_line ON iot_devices(line_id);
CREATE INDEX idx_device_status ON iot_devices(status);

CREATE TABLE iot_sensors (
    id              BIGSERIAL PRIMARY KEY,
    device_id       BIGINT NOT NULL REFERENCES iot_devices(id) ON DELETE CASCADE,
    sensor_code     VARCHAR(64) NOT NULL,
    sensor_name     VARCHAR(128) NOT NULL,
    data_type       VARCHAR(16) NOT NULL,   -- INT / FLOAT / BOOL / STRING
    unit            VARCHAR(32),
    register_addr   VARCHAR(64),
    scale_factor    NUMERIC(10,4) DEFAULT 1.0,
    addr_offset     NUMERIC(10,4) DEFAULT 0.0,
    min_value       NUMERIC(12,3),
    max_value       NUMERIC(12,3),
    alarm_low       NUMERIC(12,3),
    alarm_high      NUMERIC(12,3),
    alarm_low_low   NUMERIC(12,3),
    alarm_high_high NUMERIC(12,3),
    sample_interval INT DEFAULT 1000,
    is_key_metric   BOOLEAN DEFAULT FALSE,
    status          SMALLINT DEFAULT 1,
    UNIQUE(device_id, sensor_code)
);
CREATE INDEX idx_sensor_device ON iot_sensors(device_id);

-- ============ 原始采集数据 (按天分区) ============
CREATE TABLE iot_raw_data (
    id              BIGSERIAL,
    device_id       BIGINT NOT NULL,
    sensor_id       BIGINT NOT NULL,
    line_id         BIGINT,
    work_order_id   BIGINT,
    value_str       TEXT,
    value_num       DOUBLE PRECISION,
    quality         SMALLINT DEFAULT 192,   -- OPC-UA Quality
    collected_at    TIMESTAMPTZ NOT NULL,
    ingested_at     TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (id, collected_at)          -- 分区表主键必须包含分区列
) PARTITION BY RANGE (collected_at);

-- 动态预建今明两天分区; 后续由 pg_partman 按天 premake
DO $$
DECLARE
    today DATE := now()::date;
    d DATE;
BEGIN
    FOR i IN 0..1 LOOP
        d := today + i;
        EXECUTE format(
            'CREATE TABLE IF NOT EXISTS iot_raw_data_p%s PARTITION OF iot_raw_data '
            'FOR VALUES FROM (%L) TO (%L)',
            to_char(d, 'YYYYMMDD'), d, d + 1);
    END LOOP;
END $$;

CREATE INDEX idx_raw_device_time ON iot_raw_data(device_id, collected_at DESC);
CREATE INDEX idx_raw_sensor_time ON iot_raw_data(sensor_id, collected_at DESC);
CREATE INDEX idx_raw_wo ON iot_raw_data(work_order_id) WHERE work_order_id IS NOT NULL;

-- 注册 pg_partman 按天分区 (premake=7, 保留 90 天)
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_partman') THEN
        PERFORM partman.create_parent(
            p_parent_table := 'public.iot_raw_data',
            p_control := 'collected_at',
            p_type := 'range',
            p_interval := '1 day',
            p_premake := 7,
            -- 从今天开始: 复用上面已预建的今明两天分区 (命名对齐 partman 5.x 固定后缀 _pYYYYMMDD)
            p_start_partition := to_char(now()::date, 'YYYY-MM-DD'));
        PERFORM cron.schedule('partman-maintenance-iot', '*/5 * * * *',
            $cron$SELECT partman.run_maintenance('public.iot_raw_data')$cron$);
        UPDATE partman.part_config
           SET retention = '90 days', retention_keep_table = FALSE
         WHERE parent_table = 'public.iot_raw_data';
    ELSE
        RAISE NOTICE 'pg_partman 不可用: iot_raw_data 仅预建分区';
    END IF;
EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'pg_partman 注册失败 (%), 降级为手工预建分区', SQLERRM;
END $$;

-- ============ 告警 ============
CREATE TABLE iot_alerts (
    id              BIGSERIAL PRIMARY KEY,
    device_id       BIGINT NOT NULL,
    sensor_id       BIGINT,
    alert_type      VARCHAR(32) NOT NULL,   -- HIGH / LOW / HIGH_HIGH / LOW_LOW / OFFLINE / ERROR
    alert_level     SMALLINT NOT NULL,      -- 1:提示 2:警告 3:严重 4:致命
    alert_value     DOUBLE PRECISION,
    threshold       DOUBLE PRECISION,
    message         VARCHAR(512),
    status          SMALLINT DEFAULT 0,     -- 0:未处理 1:已确认 2:已消除 3:已忽略
    acknowledged_by BIGINT,
    acknowledged_at TIMESTAMPTZ,
    resolved_at     TIMESTAMPTZ,
    duration_sec    INT,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_alert_device ON iot_alerts(device_id);
CREATE INDEX idx_alert_status ON iot_alerts(status);
CREATE INDEX idx_alert_level ON iot_alerts(alert_level, status);
CREATE INDEX idx_alert_created ON iot_alerts(created_at);

-- ============ 采集任务 ============
CREATE TABLE iot_collection_tasks (
    id              BIGSERIAL PRIMARY KEY,
    task_code       VARCHAR(64) NOT NULL UNIQUE,
    task_name       VARCHAR(128) NOT NULL,
    protocol        VARCHAR(32) NOT NULL,
    schedule_type   SMALLINT DEFAULT 1,    -- 1:周期采集 2:事件触发 3:按需
    interval_ms     INT DEFAULT 1000,
    config          JSONB,
    enabled         BOOLEAN DEFAULT TRUE,
    last_run_at     TIMESTAMPTZ,
    next_run_at     TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE iot_task_devices (
    task_id         BIGINT NOT NULL REFERENCES iot_collection_tasks(id) ON DELETE CASCADE,
    device_id       BIGINT NOT NULL REFERENCES iot_devices(id),
    PRIMARY KEY (task_id, device_id)
);
CREATE INDEX idx_itd_device ON iot_task_devices(device_id);
