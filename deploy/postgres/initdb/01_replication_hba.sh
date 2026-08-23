#!/bin/bash
# 持久化 pg_hba.conf replication 条目 (允许 replica 从 Docker 网络连接)
# 解决: postgres-replica pg_basebackup 时 'no pg_hba.conf entry for replication connection'
echo "host replication mes 0.0.0.0/0 md5" >> "$PGDATA/pg_hba.conf"
