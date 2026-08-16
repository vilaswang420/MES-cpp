-- 006_integ_tables.up.sql — ERP/WMS 集成 4 表 + WebSocket 会话表
-- 设计事实源: MES_Architecture_Design.md 3.6 / 3.7.2 节

CREATE TABLE integ_api_configs (
    id              BIGSERIAL PRIMARY KEY,
    system_type     VARCHAR(32) NOT NULL,  -- ERP / WMS
    system_name     VARCHAR(64) NOT NULL,
    base_url        VARCHAR(256) NOT NULL,
    auth_type       VARCHAR(32) DEFAULT 'Bearer',
    token_key       VARCHAR(512),
    token_expire_at TIMESTAMPTZ,
    timeout_ms      INT DEFAULT 10000,
    retry_count     INT DEFAULT 3,
    enabled         BOOLEAN DEFAULT TRUE,
    config          JSONB,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE integ_erp_orders (
    id              BIGSERIAL PRIMARY KEY,
    erp_order_no    VARCHAR(64) NOT NULL UNIQUE,
    erp_order_type  SMALLINT NOT NULL,     -- 1:生产订单 2:采购订单
    product_code    VARCHAR(64) NOT NULL,
    product_name    VARCHAR(256),
    quantity        INT NOT NULL,
    unit            VARCHAR(32),
    plan_start_date DATE,
    plan_end_date   DATE,
    priority        SMALLINT DEFAULT 5,
    customer_name   VARCHAR(128),
    status          SMALLINT DEFAULT 0,    -- 0:待同步 1:已转工单 2:已同步 3:已取消
    work_order_id   BIGINT,
    raw_data        JSONB,
    synced_at       TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_erp_order_status ON integ_erp_orders(status);

-- 主数据边界: MES 不维护本地物料/BOM, 权威数据在 ERP; 本表仅引用 material_code
CREATE TABLE integ_wms_inventory (
    id              BIGSERIAL PRIMARY KEY,
    material_code   VARCHAR(64) NOT NULL,
    material_name   VARCHAR(256),
    warehouse       VARCHAR(64),
    location_code   VARCHAR(64),
    batch_no        VARCHAR(64),
    quantity        NUMERIC(12,3) NOT NULL,
    unit            VARCHAR(32),
    status          VARCHAR(16) DEFAULT 'AVAILABLE',
    sync_type       SMALLINT NOT NULL,    -- 1:物料领用 2:成品入库 3:库存调整
    work_order_id   BIGINT,
    raw_data        JSONB,
    synced_at       TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_wms_material ON integ_wms_inventory(material_code);
CREATE INDEX idx_wms_wo ON integ_wms_inventory(work_order_id);

CREATE TABLE integ_sync_logs (
    id              BIGSERIAL PRIMARY KEY,
    system_type     VARCHAR(32) NOT NULL,  -- ERP / WMS
    sync_direction  SMALLINT NOT NULL,     -- 1:接收 2:发送
    sync_type       VARCHAR(64),
    business_id     BIGINT,
    request_url     VARCHAR(512),
    request_body    TEXT,
    response_body   TEXT,
    http_status     INT,
    duration_ms     INT,
    status          SMALLINT DEFAULT 0,    -- 0:失败 1:成功 2:重试中
    retry_count     INT DEFAULT 0,
    error_msg       TEXT,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_sync_log_system ON integ_sync_logs(system_type, created_at);
CREATE INDEX idx_sync_log_status ON integ_sync_logs(status) WHERE status != 1;

-- WebSocket 会话登记表 (运行时订阅关系在 Redis; 本表用于审计与连接追溯)
CREATE TABLE sys_websocket_sessions (
    id              BIGSERIAL PRIMARY KEY,
    session_id      VARCHAR(128) NOT NULL UNIQUE,
    user_id         BIGINT NOT NULL,
    client_type     SMALLINT DEFAULT 1,    -- 1:管理后台 2:大屏看板
    dashboard_id    BIGINT,
    subscriptions   TEXT[],
    ip_address      INET,
    connected_at    TIMESTAMPTZ DEFAULT NOW(),
    last_active_at  TIMESTAMPTZ DEFAULT NOW(),
    disconnected_at TIMESTAMPTZ
);
CREATE INDEX idx_ws_session_user ON sys_websocket_sessions(user_id);
CREATE INDEX idx_ws_session_active ON sys_websocket_sessions(session_id)
    WHERE disconnected_at IS NULL;
