# 定制 PostgreSQL 16 镜像: pg_partman (自动分区) + pg_cron (定时维护)
# S0.2 验证点: partman.create_parent / cron.schedule / 审计表跨分区写入
FROM postgres:16

ENV PG_PARTMAN_VERSION=5.1.0

# 切换 Debian apt 源为阿里云镜像 (国内直连 deb.debian.org / apt.postgresql.org 极慢)
RUN set -eux; \
    sed -i 's|http://deb.debian.org|http://mirrors.aliyun.com|g' /etc/apt/sources.list.d/*.sources; \
    sed -i 's|http://apt.postgresql.org|https://mirrors.aliyun.com/postgresql|g' /etc/apt/sources.list.d/*.sources; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        build-essential postgresql-server-dev-16 \
        ca-certificates curl git make; \
    # ---- pg_partman (GitHub 直连不稳定, 超时重试 + 加速镜像回退) ----
    (curl -fsSL --connect-timeout 15 --retry 3 "https://github.com/pgpartman/pg_partman/archive/refs/tags/v${PG_PARTMAN_VERSION}.tar.gz" -o /tmp/partman.tar.gz \
     || curl -fsSL --connect-timeout 15 --retry 3 "https://ghfast.top/https://github.com/pgpartman/pg_partman/archive/refs/tags/v${PG_PARTMAN_VERSION}.tar.gz" -o /tmp/partman.tar.gz); \
    mkdir -p /tmp/partman && tar -xzf /tmp/partman.tar.gz -C /tmp/partman --strip-components=1; \
    cd /tmp/partman && make && make install; \
    # ---- pg_cron (同样带回退) ----
    (git clone --branch v1.6.4 --depth 1 https://github.com/citusdata/pg_cron.git /tmp/pg_cron \
     || git clone --branch v1.6.4 --depth 1 https://ghfast.top/https://github.com/citusdata/pg_cron.git /tmp/pg_cron); \
    cd /tmp/pg_cron && make && make install; \
    # ---- 清理 ----
    apt-get purge -y build-essential postgresql-server-dev-16; \
    apt-get autoremove -y; rm -rf /var/lib/apt/lists/* /tmp/partman* /tmp/pg_cron

COPY initdb/ /docker-entrypoint-initdb.d/
COPY init_replica.sh /init_replica.sh
RUN chmod +x /init_replica.sh

# pg_cron 必须注册到 shared_preload_libraries; cron 库内跑在 mes 库
ENV POSTGRES_DB=mes \
     POSTGRES_USER=mes \
     POSTGRES_PASSWORD=mes_dev_pwd

CMD ["postgres", "-c", "shared_preload_libraries=pg_cron", "-c", "cron.database_name=mes"]
