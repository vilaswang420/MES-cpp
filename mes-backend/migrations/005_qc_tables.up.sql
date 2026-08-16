-- 005_qc_tables.up.sql — 质量管理 4 表
-- 设计事实源: MES_Architecture_Design.md 3.5 节

CREATE TABLE qc_inspection_standards (
    id              BIGSERIAL PRIMARY KEY,
    standard_code   VARCHAR(64) NOT NULL UNIQUE,
    standard_name   VARCHAR(128) NOT NULL,
    product_id      BIGINT REFERENCES prod_products(id),
    process_step_id BIGINT REFERENCES prod_process_steps(id),
    inspection_type SMALLINT DEFAULT 1,   -- 1:首件检验 2:过程检验 3:完工检验 4:抽样检验
    sample_size     INT DEFAULT 1,
    aql_level       VARCHAR(16),
    status          SMALLINT DEFAULT 1,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE qc_inspection_items (
    id              BIGSERIAL PRIMARY KEY,
    standard_id     BIGINT NOT NULL REFERENCES qc_inspection_standards(id) ON DELETE CASCADE,
    item_name       VARCHAR(128) NOT NULL,
    item_code       VARCHAR(64) NOT NULL,
    data_type       VARCHAR(16) NOT NULL,  -- NUMERIC / BOOL / TEXT
    upper_limit     NUMERIC(12,4),
    lower_limit     NUMERIC(12,4),
    nominal_value   NUMERIC(12,4),
    unit            VARCHAR(32),
    is_key_item     BOOLEAN DEFAULT FALSE,
    sort_order      INT DEFAULT 0,
    UNIQUE(standard_id, item_code)
);

CREATE TABLE qc_inspections (
    id                  BIGSERIAL PRIMARY KEY,
    inspection_no       VARCHAR(64) NOT NULL UNIQUE,
    standard_id         BIGINT REFERENCES qc_inspection_standards(id),
    work_order_id       BIGINT REFERENCES prod_work_orders(id),
    operation_id        BIGINT REFERENCES prod_work_order_operations(id),
    product_id          BIGINT REFERENCES prod_products(id),
    inspector_id        BIGINT NOT NULL,
    inspection_type     SMALLINT NOT NULL,
    sample_qty          INT DEFAULT 1,
    pass_qty            INT DEFAULT 0,
    defect_qty          INT DEFAULT 0,
    result              SMALLINT DEFAULT 0,  -- 0:待检 1:合格 2:不合格 3:让步接收
    remark              TEXT,
    inspected_at        TIMESTAMPTZ DEFAULT NOW(),
    created_at          TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_insp_wo ON qc_inspections(work_order_id);
CREATE INDEX idx_insp_result ON qc_inspections(result);

CREATE TABLE qc_defects (
    id              BIGSERIAL PRIMARY KEY,
    inspection_id   BIGINT NOT NULL REFERENCES qc_inspections(id) ON DELETE CASCADE,
    work_order_id   BIGINT REFERENCES prod_work_orders(id),
    defect_code     VARCHAR(64) NOT NULL,
    defect_name     VARCHAR(128) NOT NULL,
    defect_category VARCHAR(64),
    quantity        INT DEFAULT 1,
    severity        SMALLINT DEFAULT 2,  -- 1:轻微 2:一般 3:严重 4:致命
    disposition     SMALLINT DEFAULT 0,  -- 0:待处理 1:返工 2:返修 3:报废 4:让步
    root_cause      TEXT,
    corrective_action TEXT,
    station_id      BIGINT,
    operator_id     BIGINT,
    created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_defect_wo ON qc_defects(work_order_id);
CREATE INDEX idx_defect_category ON qc_defects(defect_category);
