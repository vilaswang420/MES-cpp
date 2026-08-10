-- 001_auth_tables.up.sql
-- A 域 8 表 + mq_outbox + sys_configs; sys_audit_logs 按月分区并注册 pg_partman
-- 设计事实源: HMS_Architecture_Design.md 3.2 / 3.7 / 7.5 节

-- ============ 部门 ============
CREATE TABLE sys_departments (
    id              BIGSERIAL PRIMARY KEY,
    parent_id       BIGINT REFERENCES sys_departments(id) DEFAULT NULL,  -- NULL 表示顶级部门
    dept_code       VARCHAR(64) NOT NULL UNIQUE,
    dept_name       VARCHAR(128) NOT NULL,
    sort_order      INT DEFAULT 0,
    leader_id       BIGINT,
    phone           VARCHAR(20),
    status          SMALLINT DEFAULT 1,  -- 0:禁用 1:启用
    deleted         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_dept_parent ON sys_departments(parent_id);

-- ============ 用户 ============
CREATE TABLE sys_users (
    id              BIGSERIAL PRIMARY KEY,
    dept_id         BIGINT REFERENCES sys_departments(id),
    username        VARCHAR(64) NOT NULL UNIQUE,
    password_hash   VARCHAR(256) NOT NULL,
    real_name       VARCHAR(64) NOT NULL,
    employee_no     VARCHAR(32) UNIQUE,
    email           VARCHAR(128),
    phone           VARCHAR(20),
    avatar_url      VARCHAR(512),
    gender          SMALLINT DEFAULT 0,  -- 0:未知 1:男 2:女
    status          SMALLINT DEFAULT 1,  -- 0:禁用 1:启用 2:锁定
    last_login_at   TIMESTAMPTZ,
    last_login_ip   INET,
    login_fail_count INT DEFAULT 0,
    password_changed_at TIMESTAMPTZ DEFAULT NOW(),
    deleted         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_user_dept ON sys_users(dept_id);
CREATE INDEX idx_user_status ON sys_users(status) WHERE deleted = FALSE;

-- ============ 角色 ============
CREATE TABLE sys_roles (
    id              BIGSERIAL PRIMARY KEY,
    role_code       VARCHAR(64) NOT NULL UNIQUE,
    role_name       VARCHAR(128) NOT NULL,
    description     TEXT,
    data_scope      SMALLINT DEFAULT 1,
    -- 1:仅本人 2:本部门 3:本部门及子部门 4:全部 5:自定义
    sort_order      INT DEFAULT 0,
    status          SMALLINT DEFAULT 1,
    deleted         BOOLEAN DEFAULT FALSE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ============ 权限 ============
CREATE TABLE sys_permissions (
    id              BIGSERIAL PRIMARY KEY,
    parent_id       BIGINT REFERENCES sys_permissions(id) DEFAULT NULL,  -- NULL 表示根权限
    perm_code       VARCHAR(128) NOT NULL,   -- 权限码; 同一权限码可映射多个接口路由
    perm_name       VARCHAR(128) NOT NULL,
    perm_type       SMALLINT NOT NULL,  -- 1:菜单 2:按钮 3:接口
    path            VARCHAR(256),       -- 菜单路由或接口路径
    method          VARCHAR(10),        -- 接口类型 (GET/POST/PUT/DELETE)
    icon            VARCHAR(64),
    sort_order      INT DEFAULT 0,
    visible         BOOLEAN DEFAULT TRUE,
    status          SMALLINT DEFAULT 1,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
-- 接口权限按 (权限码+路径+方法) 唯一; COALESCE 使菜单/按钮(NULL path/method)同样受约束
CREATE UNIQUE INDEX uk_perm_entry ON sys_permissions (perm_code, COALESCE(path,''), COALESCE(method,''));
CREATE INDEX idx_perm_parent ON sys_permissions(parent_id);
CREATE INDEX idx_perm_type ON sys_permissions(perm_type);
CREATE INDEX idx_perm_code ON sys_permissions(perm_code);

-- ============ 用户-角色 / 角色-权限 / 角色数据范围 ============
CREATE TABLE sys_user_roles (
    id              BIGSERIAL PRIMARY KEY,
    user_id         BIGINT NOT NULL REFERENCES sys_users(id) ON DELETE CASCADE,
    role_id         BIGINT NOT NULL REFERENCES sys_roles(id) ON DELETE CASCADE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(user_id, role_id)
);
CREATE INDEX idx_user_roles_user ON sys_user_roles(user_id);
CREATE INDEX idx_user_roles_role ON sys_user_roles(role_id);

CREATE TABLE sys_role_permissions (
    id              BIGSERIAL PRIMARY KEY,
    role_id         BIGINT NOT NULL REFERENCES sys_roles(id) ON DELETE CASCADE,
    permission_id   BIGINT NOT NULL REFERENCES sys_permissions(id) ON DELETE CASCADE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(role_id, permission_id)
);
CREATE INDEX idx_role_perm_role ON sys_role_permissions(role_id);
CREATE INDEX idx_role_perm_perm ON sys_role_permissions(permission_id);

CREATE TABLE sys_role_dept_scope (
    id              BIGSERIAL PRIMARY KEY,
    role_id         BIGINT NOT NULL REFERENCES sys_roles(id) ON DELETE CASCADE,
    dept_id         BIGINT NOT NULL REFERENCES sys_departments(id) ON DELETE CASCADE,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(role_id, dept_id)
);

-- ============ 审计日志 (按月分区) ============
-- 分区表主键必须包含分区列 (PostgreSQL 约束)
CREATE TABLE sys_audit_logs (
    id              BIGSERIAL,
    user_id         BIGINT,
    username        VARCHAR(64),
    module          VARCHAR(64),        -- 模块名
    operation       VARCHAR(64),        -- 操作类型
    method          VARCHAR(10),        -- HTTP 方法
    request_url     VARCHAR(512),
    request_params  TEXT,               -- 请求参数 JSON
    response_code   INT,                -- 响应状态码
    error_msg       TEXT,
    ip_address      INET,
    user_agent      VARCHAR(512),
    duration_ms     INT,                -- 耗时(毫秒)
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (id, created_at)
) PARTITION BY RANGE (created_at);

-- 动态预建当月与次月分区 (迁移可在任意月份执行; 后续由 pg_partman premake 预热)
DO $$
DECLARE
    cur_month DATE := date_trunc('month', now());
    m DATE;
BEGIN
    FOR i IN 0..1 LOOP
        m := cur_month + (i || ' months')::interval;
        EXECUTE format(
            'CREATE TABLE IF NOT EXISTS sys_audit_logs_%s PARTITION OF sys_audit_logs '
            'FOR VALUES FROM (%L) TO (%L)',
            to_char(m, 'YYYY_MM'), m, m + interval '1 month');
    END LOOP;
END $$;

CREATE INDEX idx_audit_user ON sys_audit_logs(user_id);
CREATE INDEX idx_audit_created ON sys_audit_logs(created_at);

-- 注册 pg_partman 自动按月分区 (镜像缺扩展时降级为仅预建分区, 由 CI/部署断言兜底)
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_partman') THEN
        PERFORM partman.create_parent(
            p_parent_table := 'public.sys_audit_logs',
            p_control := 'created_at',
            p_type := 'range',
            p_interval := '1 month',
            p_premake := 3);
        -- 统一 cron 维护: 分区自动创建与过期回收 (保留 24 个月)
        PERFORM cron.schedule('partman-maintenance-audit', '*/5 * * * *',
            $$SELECT partman.run_maintenance('public.sys_audit_logs')$$);
        UPDATE partman.part_config
           SET retention = '24 months', retention_keep_table = FALSE
         WHERE parent_table = 'public.sys_audit_logs';
    ELSE
        RAISE NOTICE 'pg_partman 不可用: sys_audit_logs 仅预建分区, 请确认部署使用定制镜像';
    END IF;
EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'pg_partman 注册失败 (%), 降级为手工预建分区', SQLERRM;
END $$;

-- ============ MQ Outbox (7.5 节事务消息一致性) ============
CREATE TABLE mq_outbox (
    id              BIGSERIAL PRIMARY KEY,
    exchange        VARCHAR(64) NOT NULL,
    routing_key     VARCHAR(128) NOT NULL,
    payload         TEXT NOT NULL,
    status          SMALLINT DEFAULT 0,    -- 0:待投递 1:已投递 2:失败
    retry_count     INT DEFAULT 0,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    sent_at         TIMESTAMPTZ
);
CREATE INDEX idx_outbox_pending ON mq_outbox(created_at) WHERE status = 0;

-- ============ 系统配置 ============
CREATE TABLE sys_configs (
    id              BIGSERIAL PRIMARY KEY,
    config_key      VARCHAR(128) NOT NULL UNIQUE,
    config_value    TEXT,
    config_type     VARCHAR(16) DEFAULT 'STRING',  -- STRING/INT/BOOL/JSON
    description     VARCHAR(256),
    category        VARCHAR(64),
    is_system       BOOLEAN DEFAULT FALSE,  -- 系统内置不可删
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);
