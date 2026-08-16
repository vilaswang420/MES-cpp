-- 017_oee_stats.up.sql — P4-5.4 真实 OEE 计算消费者
-- ISO 22400: OEE = A(可用率) × P(表现性) × Q(质量率), 聚合粒度 (line_id, stat_date, shift)
-- 数值口径: 百分比 0-100 (NUMERIC(6,3))

CREATE TABLE prod_oee_stats (
    id               BIGSERIAL PRIMARY KEY,
    line_id          BIGINT NOT NULL REFERENCES prod_production_lines(id),
    stat_date        DATE NOT NULL,
    shift            SMALLINT NOT NULL,      -- 1:早班 2:中班 3:夜班
    run_seconds      NUMERIC(12,2) DEFAULT 0,  -- A: run_status=1 累计运行秒数 (瓶颈设备)
    planned_seconds  NUMERIC(12,2) DEFAULT 0,  -- A: 计划生产秒数 (班次数 × 8h)
    report_seconds   NUMERIC(12,2) DEFAULT 0,  -- P: Σ(报工量 × 标准节拍) 秒数
    pass_qty         INT DEFAULT 0,             -- Q: 质检合格数
    defect_qty       INT DEFAULT 0,             -- Q: 质检缺陷数
    availability     NUMERIC(6,3),              -- A ×100
    performance      NUMERIC(6,3),              -- P ×100
    quality          NUMERIC(6,3),              -- Q ×100
    oee              NUMERIC(6,3),              -- A×P×Q ×100
    updated_at       TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(line_id, stat_date, shift)
);

CREATE INDEX idx_oee_line_date ON prod_oee_stats(line_id, stat_date DESC);
