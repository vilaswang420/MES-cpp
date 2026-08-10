-- 003_prod_tables.up.sql — B 域生产管理 9 表
-- 设计事实源: HMS_Architecture_Design.md 3.3 节

CREATE TABLE prod_products (
    id              BIGSERIAL PRIMARY KEY,
    product_code    VARCHAR(64) NOT NULL UNIQUE,
    product_name    VARCHAR(256) NOT NULL,
    specification   VARCHAR(512),       -- 规格型号
    unit            VARCHAR(32) DEFAULT 'PCS',
    category        VARCHAR(64),        -- 产品分类
    process_id      BIGINT,             -- 当前生效工艺路线(指向 prod_processes, 1:N)
    erp_material_code VARCHAR(64),      -- ERP 物料编码
    status          SMALLINT DEFAULT 1,
    deleted         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_product_code ON prod_products(product_code);

CREATE TABLE prod_production_lines (
    id              BIGSERIAL PRIMARY KEY,
    line_code       VARCHAR(64) NOT NULL UNIQUE,
    line_name       VARCHAR(128) NOT NULL,
    workshop        VARCHAR(64),        -- 车间
    location        VARCHAR(256),       -- 位置
    capacity_per_hour INT,              -- 每小时产能
    status          SMALLINT DEFAULT 1, -- 0:停线 1:运行 2:保养
    deleted         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE prod_workstations (
    id              BIGSERIAL PRIMARY KEY,
    line_id         BIGINT NOT NULL REFERENCES prod_production_lines(id),
    station_code    VARCHAR(64) NOT NULL,
    station_name    VARCHAR(128) NOT NULL,
    station_seq     INT NOT NULL,       -- 工位顺序
    device_id       BIGINT,             -- 关联设备 (004 迁移后为逻辑引用)
    std_cycle_time  INT,                -- 标准节拍(秒)
    status          SMALLINT DEFAULT 1,
    deleted         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(line_id, station_code)
);
CREATE INDEX idx_ws_line ON prod_workstations(line_id);

CREATE TABLE prod_processes (
    id              BIGSERIAL PRIMARY KEY,
    process_code    VARCHAR(64) NOT NULL UNIQUE,
    process_name    VARCHAR(128) NOT NULL,
    product_id      BIGINT REFERENCES prod_products(id),
    version         VARCHAR(16) DEFAULT '1.0',
    total_steps     INT NOT NULL,
    status          SMALLINT DEFAULT 1,  -- 0:草稿 1:已发布 2:已废弃
    published_at    TIMESTAMPTZ,
    created_by      BIGINT,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE prod_process_steps (
    id              BIGSERIAL PRIMARY KEY,
    process_id      BIGINT NOT NULL REFERENCES prod_processes(id) ON DELETE CASCADE,
    step_seq        INT NOT NULL,           -- 工序序号
    step_name       VARCHAR(128) NOT NULL,
    step_code       VARCHAR(64) NOT NULL,
    workstation_type VARCHAR(64),           -- 所需工位类型
    std_cycle_time  INT,                    -- 标准节拍(秒)
    description     TEXT,
    quality_check   BOOLEAN DEFAULT FALSE,  -- 是否需要质检
    is_key_step     BOOLEAN DEFAULT FALSE,  -- 是否关键工序
    UNIQUE(process_id, step_seq)
);

-- 补充产品表工艺外键 (建表顺序: 先产品后工艺, 外键后置添加)
ALTER TABLE prod_products
    ADD CONSTRAINT fk_product_process FOREIGN KEY (process_id) REFERENCES prod_processes(id);

CREATE TABLE prod_work_orders (
    id                  BIGSERIAL PRIMARY KEY,
    work_order_no       VARCHAR(64) NOT NULL UNIQUE,
    erp_order_id        BIGINT,                 -- 关联 ERP 订单
    erp_order_no        VARCHAR(64),            -- ERP 订单号
    product_id          BIGINT NOT NULL REFERENCES prod_products(id),
    process_id          BIGINT REFERENCES prod_processes(id),
    line_id             BIGINT REFERENCES prod_production_lines(id),
    plan_qty            INT NOT NULL,           -- 计划数量
    completed_qty       INT DEFAULT 0,          -- 完成数量
    good_qty            INT DEFAULT 0,          -- 合格数量
    defect_qty          INT DEFAULT 0,          -- 不良数量
    scrap_qty           INT DEFAULT 0,          -- 报废数量
    plan_start_at       TIMESTAMPTZ,
    plan_end_at         TIMESTAMPTZ,
    actual_start_at     TIMESTAMPTZ,
    actual_end_at       TIMESTAMPTZ,
    priority            SMALLINT DEFAULT 5,     -- 1-9, 默认5
    status              SMALLINT DEFAULT 0,
    -- 0:待排产 1:已排产 2:已下达 3:进行中 4:已暂停 5:已完工 6:已关闭 7:已取消
    source              SMALLINT DEFAULT 1,     -- 1:ERP 2:手动创建
    remark              TEXT,
    created_by          BIGINT,
    created_at          TIMESTAMPTZ DEFAULT NOW(),
    updated_at          TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_wo_status ON prod_work_orders(status);
CREATE INDEX idx_wo_line ON prod_work_orders(line_id);
CREATE INDEX idx_wo_erp ON prod_work_orders(erp_order_id) WHERE erp_order_id IS NOT NULL;
CREATE INDEX idx_wo_plan_start ON prod_work_orders(plan_start_at);

CREATE TABLE prod_work_order_operations (
    id                  BIGSERIAL PRIMARY KEY,
    work_order_id       BIGINT NOT NULL REFERENCES prod_work_orders(id) ON DELETE CASCADE,
    process_step_id     BIGINT REFERENCES prod_process_steps(id),
    workstation_id      BIGINT REFERENCES prod_workstations(id),
    step_seq            INT NOT NULL,
    step_name           VARCHAR(128) NOT NULL,
    plan_qty            INT NOT NULL,
    completed_qty       INT DEFAULT 0,
    good_qty            INT DEFAULT 0,
    defect_qty          INT DEFAULT 0,
    operator_id         BIGINT,               -- 操作员
    plan_start_at       TIMESTAMPTZ,
    plan_end_at         TIMESTAMPTZ,
    actual_start_at     TIMESTAMPTZ,
    actual_end_at       TIMESTAMPTZ,
    status              SMALLINT DEFAULT 0,
    -- 0:待开始 1:进行中 2:已完工 3:已跳过
    UNIQUE(work_order_id, step_seq)
);
CREATE INDEX idx_woo_wo ON prod_work_order_operations(work_order_id);
CREATE INDEX idx_woo_station ON prod_work_order_operations(workstation_id);
CREATE INDEX idx_woo_status ON prod_work_order_operations(status);

CREATE TABLE prod_production_plans (
    id              BIGSERIAL PRIMARY KEY,
    plan_no         VARCHAR(64) NOT NULL UNIQUE,
    plan_date       DATE NOT NULL,
    line_id         BIGINT NOT NULL REFERENCES prod_production_lines(id),
    shift           SMALLINT NOT NULL,     -- 1:早班 2:中班 3:夜班
    plan_qty        INT NOT NULL,
    status          SMALLINT DEFAULT 0,    -- 0:草稿 1:已确认 2:已执行 3:已取消
    created_by      BIGINT,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_plan_date_line ON prod_production_plans(plan_date, line_id);

-- 计划-工单关联表 (替代 BIGINT[] 数组, 保证引用完整性)
CREATE TABLE prod_plan_work_orders (
    plan_id         BIGINT NOT NULL REFERENCES prod_production_plans(id) ON DELETE CASCADE,
    work_order_id   BIGINT NOT NULL REFERENCES prod_work_orders(id),
    PRIMARY KEY (plan_id, work_order_id)
);
CREATE INDEX idx_ppw_wo ON prod_plan_work_orders(work_order_id);
