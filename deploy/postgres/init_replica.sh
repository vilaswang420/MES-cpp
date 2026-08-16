#!/usr/bin/env bash
# M3 任务 25 读写分离: PG 只读副本初始化 (pg_basebackup + standby.signal)
# 用法: 首次启动 postgres-replica 前执行一次 (数据卷为空时)。
#   docker compose -f deploy/compose/docker-compose.prod.yml run --rm \
#     postgres-replica bash /docker-entrypoint-initdb.d/../init_replica.sh
# 或在宿主机 docker run 挂载 mes_pg_replica 卷执行。
set -euo pipefail

PGDATA="${PGDATA:-/var/lib/postgresql/data}"
PRIMARY="${PRIMARY_HOST:-postgres-primary}"
REPL_USER="${REPL_USER:-mes}"
REPL_PASS="${REPL_PASSWORD:?set REPL_PASSWORD (= MES_PG_PASSWORD)}"

if [ -s "$PGDATA/PG_VERSION" ]; then
    echo "数据目录已初始化, 跳过 (如需重建请先清空卷 mes_pg_replica)"
    exit 0
fi

echo "等待主库就绪..."
until PGPASSWORD="$REPL_PASS" pg_isready -h "$PRIMARY" -U "$REPL_USER"; do sleep 2; done

rm -rf "$PGDATA"
PGPASSWORD="$REPL_PASS" pg_basebackup -h "$PRIMARY" -U "$REPL_USER" -D "$PGDATA" \
    -X stream -R -C -S mes_replica_slot
# -R 生成 standby.signal + primary_conninfo; -C 创建复制槽防 WAL 过早回收
chown -R postgres:postgres "$PGDATA" && chmod 700 "$PGDATA"
echo "副本初始化完成, 可启动 postgres-replica"
