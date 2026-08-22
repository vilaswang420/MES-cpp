# MES 制造执行系统 — Linux Ubuntu 24.04 部署完整手册

> 版本: 1.0 | 日期: 2026-08-15 | 适用: MES M3 定稿 (docker-compose.prod.yml)

---

## 目录

1. [部署架构概览](#1-部署架构概览)
2. [服务器环境要求](#2-服务器环境要求)
3. [系统初始化](#3-系统初始化)
4. [获取源码与构建镜像](#4-获取源码与构建镜像)
5. [生产配置文件](#5-生产配置文件)
6. [TLS 证书与域名配置](#6-tls-证书与域名配置)
7. [首次启动与数据库迁移](#7-首次启动与数据库迁移)
8. [部署验证](#8-部署验证)
9. [HTTPS 域名正式配置 (Let's Encrypt)](#9-https-域名正式配置-lets-encrypt)
10. [可观测性与监控](#10-可观测性与监控)
11. [日常运维操作](#11-日常运维操作)
12. [备份与灾备](#12-备份与灾备)
13. [蓝绿发布与回滚](#13-蓝绿发布与回滚)
14. [故障排查](#14-故障排查)

---

## 1. 部署架构概览

```
                    Internet
                       │
                  ┌────▼────┐
                  │ Nginx   │ :443 TLS + WSS
                  │ (入口)  │ :80  → 301 HTTPS
                  └────┬────┘
            ┌──────────┼──────────┐
            │          │          │
     /srv/mes-web  /srv/mes-   /api/ + /ws
     (React SPA)   dashboard/   │
                    (Vue3)      │
                          ┌─────▼─────┐
                          │ backend   │ 单副本
                          │ (Drogon)  │ :8088
                          └─────┬─────┘
                  ┌─────────────┼─────────────┐
                  │             │             │
           ┌──────▼──┐  ┌──────▼────┐  ┌─────▼─────┐
           │PgBouncer│  │ Redis     │  │ RabbitMQ  │
           │ :6432   │  │ 单实例    │  │ 3节点     │
           │txn mode │  │           │  │ (quorum)  │
           └──────┬──┘  └───────────┘  └───────────┘
                  │
           ┌──────▼──────┐
           │ PostgreSQL  │
           │ primary     │ :5432
           │ + replica   │
           └─────────────┘

     Prometheus :9090 → backend /metrics
```

**容器清单 (docker-compose.prod.yml)**:

| 服务 | 镜像 | 副本 | 端口 | 说明 |
|------|------|------|------|------|
| postgres-primary | 定制 PG16 | 1 | 内部 | 主库 (pg_partman + pg_cron, wal_level=replica) |
| postgres-replica | 定制 PG16 | 1 | 内部 | 只读副本 / 热备 (pg_basebackup 初始化; **默认关闭**, 叠加 docker-compose.prod.ha-pg.yml 开启) |
| pgbouncer | edoburu/pgbouncer | 1 | 127.0.0.1:6432 | 连接池 (transaction 模式, 仅绑本机) |
| redis-1 | redis:7-alpine | 1 | 内部 | Redis 单实例 (Drogon 客户端不支持 Cluster) |
| rabbitmq-1~3 | rabbitmq:3.13-management | 3 | 内部 | RMQ 集群 (默认 3 节点; 4C8G 可只留 rabbitmq-1) |
| backend | mes-backend | 1 | 8088 (内部) | Drogon C++ 后端 (单副本) |
| nginx | nginx:1.27-alpine | 1 | 80, 443 | 入口代理 + 静态托管 |
| prometheus | prom/prometheus:v2.53.0 | 1 | 9090 | 指标采集 |

---

## 2. 服务器环境要求

### 2.1 硬件最低配置

| 资源 | 最低 | 推荐 | 说明 |
|------|------|------|------|
| CPU | 4 核 | 8 核 | 后端编译需大量 CPU; 运行时 4 核可支撑 ~3k QPS |
| 内存 | 8 GB | 16 GB | 默认已是精简形态 (Redis 单实例 + 无 PG replica) + RMQ 3 节点 + backend 单副本 + Prom; 详见 §3.5.6; 编译期峰值 ~6 GB |
| 磁盘 | 50 GB | 100 GB SSD | Docker 镜像 + 数据卷 + 日志; SSD 对 PG 分区表查询至关重要 |
| 网络 | 100 Mbps | 1 Gbps | IoT 数据量 + WS 长连接带宽 |

### 2.2 软件要求

| 软件 | 版本 | 安装方式 |
|------|------|---------|
| Ubuntu Server | 24.04 LTS | ISO 安装 |
| Docker Engine | 27.x+ | 官方 APT 源 |
| Docker Compose | v2.29+ (plugin) | 随 Docker 安装 |
| git | 2.43+ | apt |
| migrate (golang-migrate) | 4.17+ | 二进制下载 (仅迁移用) |
| certbot | 3.0+ | snap (仅 Let's Encrypt 证书) |
| jq | 1.7+ | apt (验证脚本用) |

---

## 3. 系统初始化

### 3.1 更新系统

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y ca-certificates curl git jq ufw
```

### 3.2 安装 Docker Engine

```bash
# 添加 Docker 官方 GPG key
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
    -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

# 添加 Docker APT 源
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
  https://download.docker.com/linux/ubuntu \
  $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# 替代方案 ：用国内镜像源（推荐，最快）。下载 Docker 官方 GPG key 被重置连接（GFW/网络问题）。
## 1. 加 key（阿里云）
curl -fsSL https://mirrors.aliyun.com/docker-ce/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

## 2. 加仓库（阿里云）
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://mirrors.aliyun.com/docker-ce/linux/ubuntu $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt update && sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# 将当前用户加入 docker 组 (免 sudo)
sudo usermod -aG docker $USER
newgrp docker

# 验证
docker --version
docker compose version
```

### 3.3 安装 golang-migrate (数据库迁移工具)

> **国内/腾讯云 VM 注意**：GitHub Releases 直连 (`objects.githubusercontent.com`) 在国内常被限速或断流，
> 表现为 `curl: (56) Failure when receiving data from the peer` + `tar: unexpected end of file`。
> 因此统一走 `ghproxy.net` 镜像加速，并**先下载到本地文件、再解包**（避免下载失败时把空流直接喂给 `tar`）。
> 如果你的环境能直连 GitHub，把 `MIGRATE_URL` 换回官方地址即可。

```bash
MIGRATE_VERSION=4.18.1
MIGRATE_URL="https://ghproxy.net/https://github.com/golang-migrate/migrate/releases/download/v${MIGRATE_VERSION}/migrate.linux-amd64.tar.gz"

# 先下载（带重试），再解包；-f 让 HTTP 错误也会中止而非静默
curl -fL --retry 3 --retry-all-errors --connect-timeout 60 -o /tmp/migrate.tar.gz "$MIGRATE_URL"
sudo tar xz -C /usr/local/bin -f /tmp/migrate.tar.gz migrate
sudo chmod +x /usr/local/bin/migrate
migrate -version
```

### 3.4 配置防火墙

```bash
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp       # SSH
sudo ufw allow 80/tcp       # HTTP (Nginx 重定向 + Let's Encrypt 验证)
sudo ufw allow 443/tcp      # HTTPS
sudo ufw allow 9090/tcp     # Prometheus (可选, 仅内网或限制来源)
sudo ufw enable
sudo ufw status verbose
```

> **注意**: RabbitMQ Management UI (15672)、PG (5432)、Redis (6379)、PgBouncer (6432) 均不暴露到宿主机, 仅 Docker 内部网络可达。如需远程调试, 通过 SSH 隧道:
> ```bash
> ssh -L 15672:localhost:15672 -L 5432:localhost:5432 user@server
> ```

### 3.5 系统参数调优（4 核 8G 单机）

> **规格前提**：本节面向 **4C8G** 单机。高可用为可配置叠加层：PG 只读副本（叠加 `docker-compose.prod.ha-pg.yml`）、Redis Cluster（叠加 `docker-compose.prod.ha-redis.yml`，**当前后端不支持，暂不可用**）。默认精简档 = Redis 单实例 + 无 PG 副本 + RMQ 3 节点 + 后端单副本 + Prometheus。
> 在 8G 下空闲内存就已逼近物理上限，加压必触发 OOM Killer。
> **4C8G 推荐组合**：① 改跑「单实例精简档」——1 PostgreSQL / 1 Redis 单节点（非 cluster）/ 1 RabbitMQ / 1 backend / 1 iot / nginx，Prometheus 可选或后期独立部署；
> ② **必须给每个容器设内存上限**（见 3.5.6，compose 默认无上限）；③ 开 2G swap 作安全垫。正式 GA 压测仍建议 ≥8C16G（见性能验证章节）。

#### 3.5.1 文件描述符与进程数

```bash
sudo tee -a /etc/security/limits.conf <<'EOF'
* soft nofile 65536
* hard nofile 65536
* soft nproc  65535
* hard nproc  65535
EOF
ulimit -n 65536   # 当前 shell 立即生效（重登终端后全局生效）
```

#### 3.5.2 内核参数（sysctl，4C8G 推荐值）

```bash
sudo tee -a /etc/sysctl.conf <<'EOF'
# 网络队列与端口范围（WS 长连接 + 容器间大量短连接）
net.core.somaxconn = 65535
net.core.netdev_max_backlog = 65535
net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.ip_local_port_range = 1024 65535
net.ipv4.tcp_tw_reuse = 1
# 内存与 OOM：允许适度 swap 兜底；Redis 官方建议 overcommit=1
vm.swappiness = 10
vm.overcommit_memory = 1
vm.dirty_background_ratio = 5
vm.dirty_ratio = 10
# PID 上限（Docker 会创建大量进程）
kernel.pid_max = 65535
EOF
sudo sysctl -p
```

#### 3.5.3 关闭透明大页 THP（Redis / PostgreSQL 必需）

THP 会导致数据库/缓存出现周期性延迟毛刺，必须关闭并持久化：

```bash
# 立即生效
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag

# 持久化（重启后仍生效）
sudo tee /etc/systemd/system/disable-thp.service <<'EOF'
[Unit]
Description=Disable Transparent Huge Pages
After=sysinit.target
[Service]
Type=oneshot
ExecStart=/bin/sh -c "echo never > /sys/kernel/mm/transparent_hugepage/enabled && echo never > /sys/kernel/mm/transparent_hugepage/defrag"
RemainAfterExit=yes
[Install]
WantedBy=multi-user.target
EOF
sudo systemctl enable --now disable-thp.service
```

#### 3.5.4 Swap 安全垫（4C8G 建议 2G）

```bash
sudo fallocate -l 2G /swapfile || sudo dd if=/dev/zero of=/swapfile bs=1M count=2048
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

#### 3.5.5 Docker daemon 调优

日志轮转防止容器日志撑爆磁盘，且 dockerd 重启时不杀容器。整段复制执行：

```bash
sudo mkdir -p /etc/docker
sudo tee /etc/docker/daemon.json <<'EOF'
{
  "log-driver": "json-file",
  "log-opts": { "max-size": "10m", "max-file": "3" },
  "live-restore": true,
  "storage-driver": "overlay2"
}
EOF
sudo systemctl restart docker
```

#### 3.5.6 容器内存上限（4C8G 必设）与高可用形态

> 生产 compose **默认未设任何内存上限**，4C8G 下任一服务吃满即 OOM。
> 注意：用 `docker compose up`（非 Swarm）时，`deploy.resources.limits` **不生效**，必须用 `mem_limit` 键。
> Redis **默认单实例**（基础文件 `docker-compose.prod.yml` 仅含 `redis-1`）；PG 只读副本 **默认关闭**（已抽到 `docker-compose.prod.ha-pg.yml`）。

**高可用是可配置的，不是删除的** —— 拆成两个独立叠加层，按需取用：

- **默认**（仅 `docker-compose.prod.yml`）：Redis 单实例 + 无 PG 副本 + RMQ 3 节点 → 适合 4C8G 起步。
- **`docker-compose.prod.ha-pg.yml`**（PG 只读副本 / 热备，立即可用）：
  ```bash
  docker compose -f docker-compose.prod.yml -f docker-compose.prod.ha-pg.yml up -d
  ```
  > ⚠ **副本当前只是热备，不是读写分离**。后端仅有一个 default db_client（指向 pgbouncer），没有任何命名 RO 客户端，也没有读写分离逻辑去查 `postgres-replica`。叠加本层后副本以 hot_standby 运行但**不会被任何只读查询使用**；故障时可手动 `promote` 提升为主库。真正的读写分流需后端新增 RO db_client 并实现分流逻辑后才生效（详见 `docker-compose.prod.ha-pg.yml` 文件头注释）。
- **`docker-compose.prod.ha-redis.yml`**（Redis Cluster 3 主 3 从，**暂不可用 ⛔**）：
  ```bash
  # 当前不要叠加！后端 hiredis 单节点客户端不支持 Cluster MOVED/ASK 重定向，
  # 开启后约 2/3 的 key 会读写出错。须待后端升级为集群客户端后再用。
  # docker compose -f docker-compose.prod.yml -f docker-compose.prod.ha-redis.yml up -d
  ```

> ⚠ **Redis Cluster 已知限制**：后端当前用 Drogon 内置 hiredis **单节点**客户端，不支持 Cluster 的 `MOVED/ASK` 重定向。一旦开启 Redis Cluster，约 2/3 的 key 会因槽位落在其他节点而读写出错。**在后端升级为集群客户端之前，建议保持 Redis 单实例**——即不叠加 `docker-compose.prod.ha-redis.yml`。

**容器内存上限**：在各 service 下手动加 `mem_limit`（示例）：

```yaml
services:
  postgres-primary:
    mem_limit: 2g
  redis-1:            # 单实例
    mem_limit: 1g
    # command 已含限制: --maxmemory 768mb --maxmemory-policy allkeys-lru
  rabbitmq-1:         # 4C8G 想再省可手动删 rabbitmq-2~3 只留单节点
    mem_limit: 1g
  pgbouncer:
    mem_limit: 256m
  backend:
    mem_limit: 512m   # 单副本
  iot:
    mem_limit: 512m
  nginx:
    mem_limit: 128m
  prometheus:         # 可选，建议后期独立或关闭
    mem_limit: 512m
```

| 服务 | 内存上限 | 说明 |
|------|---------|------|
| postgres-primary | 2g | 含 shared_buffers，占大头 |
| redis（单节点） | 1g | 已设 `maxmemory 768mb` + `allkeys-lru` 防写满 |
| rabbitmq（单节点） | 1g | Erlang VM 基线较高；3 节点则各 1g |
| pgbouncer | 256m | |
| backend | 512m | C++ Drogon 轻量（单副本） |
| iot | 512m | |
| nginx | 128m | |
| prometheus（可选） | 512m | 索引常驻，建议独立部署 |

**4C8G 内存预算**：默认精简档（Redis 单实例 + 无 replica + RMQ 3 节点）合计约 **5.5–6.5G**，为 OS / 文件系统缓存 / swap 余量留出 1.5–2.5G。若把 RMQ 删到单节点可再省 ~2G。开启 PG 副本会额外占 ~1.5–2G，此时建议 ≥8C16G。

---

## 4. 获取源码与构建镜像

### 4.1 克隆仓库

> **国内/腾讯云 VM 注意**：GitHub 直连克隆常被断流（表现为 `GnuTLS recv error (-110): The TLS connection was non-properly terminated`）。
> 因此走 `ghproxy.net` 镜像加速，并用 `--depth 1` 浅克隆把传输量压到最小（本仓库含 vcpkg，全量克隆极易超时）。
> 若后续确实需要完整提交历史，在仓库内执行 `git fetch --unshallow` 即可。

```bash
sudo mkdir -p /opt/mes
sudo chown $USER:$USER /opt/mes

# 清掉上次失败可能残留的空 .git
rm -rf /opt/mes/.git

# 镜像 + 浅克隆（推荐）
git clone --depth 1 https://ghproxy.net/https://github.com/vilaswang420/MES-cpp.git /opt/mes

# 若 ghproxy 不稳定，换以下任一镜像前缀重试：
#   git clone --depth 1 https://kgithub.com/vilaswang420/MES-cpp.git /opt/mes
#   git clone --depth 1 https://gitclone.com/github.com/vilaswang420/MES-cpp.git /opt/mes
```

### 4.2 构建后端镜像

> **重要**: 首次构建需 vcpkg 编译全部 C++ 依赖 (Drogon, SimpleAmqpClient, hiredis, jwt-cpp 等), 约需 30-60 分钟 (取决于 CPU 和网速)。后续构建有 BuildKit 缓存会快很多。

```bash
cd /opt/mes

# 构建后端镜像 (多阶段: ubuntu:24.04 编译 → ubuntu:24.04 运行)
docker build -f deploy/backend/Dockerfile -t mes-backend:latest .
```

构建成功后验证:

```bash
docker run --rm mes-backend:latest sh -c "exec 3<>/dev/tcp/127.0.0.1/8088 2>/dev/null; echo ok"
# 或直接检查二进制存在
docker run --rm mes-backend:latest ls -lh /app/mes-backend
```

### 4.3 构建前端镜像

```bash
# 管理后台 (React)
docker build -f deploy/web/Dockerfile -t mes-web:latest .

# 大屏看板 (Vue3, 注意 --base=/dashboard/ 子路径)
docker build -f deploy/dashboard/Dockerfile -t mes-dashboard:latest .
```

### 4.4 提取前端静态文件

生产环境由入口 Nginx 统一托管静态文件, 需从镜像中提取:

```bash
mkdir -p deploy/compose/web-dist deploy/compose/dashboard-dist

docker run --rm -v "$(pwd)/deploy/compose/web-dist:/out" mes-web:latest \
    sh -c "cp -r /usr/share/nginx/html/* /out/"

docker run --rm -v "$(pwd)/deploy/compose/dashboard-dist:/out" mes-dashboard:latest \
    sh -c "cp -r /usr/share/nginx/html/* /out/"
```

### 4.5 构建定制 PostgreSQL 镜像

```bash
# 定制 PG16 (含 pg_partman 5.1 + pg_cron 1.6 源码编译)
docker build -f deploy/postgres/Dockerfile -t mes-postgres:16 deploy/postgres/
```

> 如网络较慢, Dockerfile 内已内置 `ghfast.top` 代理回退。首次构建约 10-15 分钟。

---

## 5. 生产配置文件

### 5.1 环境变量文件

```bash
cd /opt/mes/deploy/compose

cat > .env <<EOF
# ---- 必须修改的密码 ----
MES_PG_PASSWORD=请替换为强密码_至少16位
MES_MQ_PASSWORD=请替换为强密码_至少16位
MES_MQ_COOKIE=请替换为随机Erlang Cookie_至少20位

# ---- 镜像版本 ----
MES_REGISTRY=
MES_VERSION=latest

# ---- 域名 (用于 Nginx server_name) ----
MES_DOMAIN=mes.yourcompany.com
EOF

chmod 600 .env  # 保护密码
```

### 5.2 后端生产配置目录

```bash
mkdir -p /opt/mes/deploy/compose/config-prod
cd /opt/mes/deploy/compose/config-prod
```

**drogon_config.json** (后端主配置):

```bash
# 从模板复制后修改密码和密钥
cp /opt/mes/mes-backend/config/drogon_config.prod.json drogon_config.json

# 修改关键项:
# 1. passwd → 实际 PG 密码 (与 .env 中 MES_PG_PASSWORD 一致)
# 2. jwt_secret → 随机 32+ 字符 (openssl rand -base64 32)
# 3. host → 保持 pgbouncer (容器名)
# 4. redis host → 保持 redis-1 (容器名)
```

使用 `jq` 快速替换:

```bash
PG_PWD="实际PG密码"
JWT_SECRET=$(openssl rand -base64 32)

jq --arg pgpwd "$PG_PWD" \
   --arg jwt "$JWT_SECRET" \
   '.db_clients[0].passwd = $pgpwd
    | .custom_config.jwt_secret = $jwt' \
   drogon_config.json > drogon_config.json.tmp && mv drogon_config.json.tmp drogon_config.json

chmod 600 drogon_config.json
```

**rabbitmq.json** (MQ 连接配置):

```bash
MQ_PWD="实际MQ密码"
jq --arg url "amqp://mes:${MQ_PWD}@rabbitmq-1:5672/%2F" \
   '.amqp_url = $url' \
   /opt/mes/mes-backend/config/rabbitmq.json > rabbitmq.json

chmod 600 rabbitmq.json
```

### 5.3 更新 RabbitMQ topology.json 中的密码 hash

RabbitMQ 的 `definitions.json` (topology.json) 使用 SHA256 hash 存储密码, 需要重新计算:

```bash
# 方法 1: 使用脚本计算 (需要 Python)
cd /opt/mes
python3 scripts/calc_mq_hash.py "$MQ_PWD"
# 输出: password_hash: xxxxx

# 方法 2: 直接用 rabbitmqctl (容器启动后)
docker exec rabbitmq-1 rabbitmqctl change_password mes "$MQ_PWD"
```

如果使用方法 2, topology.json 中的初始 hash 可以保留, 启动后立即改密码。

### 5.4 配置目录结构确认

```
deploy/compose/
├── .env                          # 环境变量 (密码等)
├── config-prod/
│   ├── drogon_config.json        # 后端配置 (PG/Redis/JWT)
│   └── rabbitmq.json             # MQ 连接配置
├── config-iot/                   # IoT 采集服务配置 (必须, 否则采集链路失效)
│   └── iot.json                  # amqp_url / backend_url / backend_pwd 等
├── web-dist/                     # React 静态文件
├── dashboard-dist/               # Vue3 静态文件
├── docker-compose.prod.yml       # 生产编排
└── ...
```

### 5.5 IoT 采集服务配置（必须）

`iot` 容器只挂载 `./config-iot` 并读取其中的 `iot.json`（由 `mes-iot/src/main.cc` 解析），**不读取任何 `MES_IOT_*` 环境变量**。若 `config-iot/iot.json` 缺失，容器空挂载会 shadow 掉镜像默认配置，退化为连 `127.0.0.1` 的骨架模式，采集整条链路失效。

```bash
cd /opt/mes/deploy/compose
mkdir -p config-iot

# 从模板复制后注入生产连接串与凭证
cp /opt/mes/mes-iot/config/iot.json config-iot/iot.json

MQ_PWD="实际MQ密码"
ADMIN_PWD="后端管理员密码"
jq --arg amqp "amqp://mes:${MQ_PWD}@rabbitmq-1:5672/" \
   --arg burl "http://backend:8088" \
   --arg bpwd "$ADMIN_PWD" \
   '.amqp_url = $amqp
    | .backend_url = $burl
    | .backend_pwd = $bpwd' \
   config-iot/iot.json > config-iot/iot.json.tmp \
   && mv config-iot/iot.json.tmp config-iot/iot.json

chmod 600 config-iot/iot.json
```

> `backend_pwd` 必须与后端 `config-prod/drogon_config.json` 中的管理员账号密码一致，否则 IoT 无法从 `/api/v1/iot/devices` 拉取设备配置。其余字段（`exchange`/`routing_key`/`batch_size`/`flush_interval_ms`/`healthz_port`）保持模板默认值即可。

---

## 6. TLS 证书与域名配置

### 6.1 方案 A: 自签证书 (内网/测试环境)

```bash
cd /opt/mes/deploy/nginx/certs

# 使用 OpenSSL 生成自签证书 (10 年有效期)
openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
    -keyout mes.key \
    -out mes.crt \
    -subj "/C=CN/ST=Province/L=City/O=YourCompany/CN=mes.local" \
    -addext "subjectAltName=DNS:mes.local,DNS:*.mes.local,IP:192.168.1.100"

chmod 600 mes.key
```

> 也可使用项目自带脚本 `scripts/gen_selfsigned_cert.ps1` (PowerShell), 但在 Linux 上用 OpenSSL 更直接。

### 6.2 方案 B: Let's Encrypt 正式证书 (生产域名)

见 [第 9 节](#9-https-域名正式配置-lets-encrypt), 首次部署可先用自签证书跑通流程, 再切换。

---

## 7. 首次启动与数据库迁移

### 7.1 首次启动（默认精简形态）

默认 `docker-compose.prod.yml` 已是精简形态：**Redis 单实例、无 PG 副本、RMQ 3 节点**。直接 `up` 即可，无需像旧版那样单独挑服务或等集群初始化。

```bash
cd /opt/mes/deploy/compose

# 默认精简形态启动 (Redis 单实例 + 无 replica)
docker compose -f docker-compose.prod.yml up -d

# 如需开启 PG 只读副本 (热备, 见 §3.5.6): 叠加 ha-pg 层
# docker compose -f docker-compose.prod.yml -f docker-compose.prod.ha-pg.yml up -d
# ⚠ Redis Cluster 需后端集群客户端支持, 当前不要叠加 docker-compose.prod.ha-redis.yml。

# 等待 PG 健康
until docker compose -f docker-compose.prod.yml exec -T postgres-primary \
    pg_isready -U mes -d mes; do
    echo "waiting for postgres..."; sleep 3
done

# 等待 Redis 就绪 (单实例)
until docker compose -f docker-compose.prod.yml exec -T redis-1 \
    redis-cli -h redis-1 ping 2>/dev/null | grep -q PONG; do
    echo "waiting for redis..."; sleep 3
done

# 等待 RMQ 集群就绪
until docker compose -f docker-compose.prod.yml exec -T rabbitmq-1 \
    rabbitmq-diagnostics -q ping 2>/dev/null; do
    echo "waiting for rabbitmq..."; sleep 3
done
```

### 7.2 初始化只读副本

```bash
# 执行 init_replica.sh (pg_basebackup + 复制槽)
docker compose -f docker-compose.prod.yml exec -T postgres-primary \
    bash /docker-entrypoint-initdb.d/init_replica.sh

# 或手动执行:
docker compose -f docker-compose.prod.yml exec -T postgres-primary bash -c '
    pg_basebackup -h 127.0.0.1 -U mes -D /tmp/replica_data -Fp -Xs -P -R \
    && touch /tmp/replica_data/standby.signal
'

# 启动 replica (需叠加 ha-pg 层, 因为 postgres-replica 定义在 docker-compose.prod.ha-pg.yml)
docker compose -f docker-compose.prod.yml -f docker-compose.prod.ha-pg.yml up -d postgres-replica
```

> **注意**: `init_replica.sh` 需要在 primary 上有复制权限。PG16 默认使用 `mes` 账号, `POSTGRES_HOST_AUTH_METHOD=md5` 允许流复制。如遇权限问题, 在 primary 上执行:
> ```sql
> ALTER ROLE mes WITH REPLICATION;
> SELECT pg_create_physical_replication_slot('replica_slot');
> ```

### 7.3 运行数据库迁移

```bash
cd /opt/mes

# 通过 PgBouncer 执行迁移 (端口 6432)
migrate \
    -path "mes-backend/migrations" \
    -database "postgres://mes:${MES_PG_PASSWORD}@localhost:6432/mes?sslmode=disable" \
    up

# 验证迁移版本
migrate \
    -path "mes-backend/migrations" \
    -database "postgres://mes:${MES_PG_PASSWORD}@localhost:6432/mes?sslmode=disable" \
    version
```

> **重要**: 迁移路径必须用正斜杠 `/`。如果 PgBouncer 未暴露端口, 可以直连 primary:
> ```bash
> # 先临时暴露 5432 端口或通过 docker exec
> docker compose -f deploy/compose/docker-compose.prod.yml exec -T postgres-primary \
>     psql -U mes -d mes -c "SELECT * FROM schema_migrations;"
> ```

### 7.4 验证分区和定时任务

```bash
docker compose -f deploy/compose/docker-compose.prod.yml exec -T postgres-primary \
    psql -U mes -d mes <<'SQL'
-- 检查 pg_partman 注册
SELECT parent_table, premake FROM partman.part_config;

-- 检查 pg_cron 定时任务
SELECT jobid, schedule, command FROM cron.job;

-- 检查分区表
SELECT tablename FROM pg_tables WHERE tablename LIKE 'iot_raw_data_%' LIMIT 5;
SELECT tablename FROM pg_tables WHERE tablename LIKE 'sys_audit_logs_%' LIMIT 5;
SQL
```

预期输出: partman 有 2 行 (iot_raw_data + sys_audit_logs), cron 有 2 个维护作业。

### 7.5 启动后端和 Nginx

```bash
cd /opt/mes/deploy/compose

# 启动后端 (单副本)
docker compose -f docker-compose.prod.yml up -d backend

# 等待后端健康
until docker compose -f docker-compose.prod.yml exec -T backend \
    sh -c "exec 3<>/dev/tcp/127.0.0.1/8088 && printf 'GET /healthz HTTP/1.0\r\n\r\n' >&3 && grep -q ok <&3" 2>/dev/null; do
    echo "waiting for backend..."; sleep 3
done

# 启动 Nginx
docker compose -f docker-compose.prod.yml up -d nginx

# 启动 Prometheus
docker compose -f docker-compose.prod.yml up -d prometheus
```

### 7.6 全栈启动 (一键)

首次部署完成上述步骤后, 后续可一键启动全栈:

```bash
cd /opt/mes/deploy/compose
docker compose -f docker-compose.prod.yml up -d
```

---

## 8. 部署验证

### 8.1 健康检查

```bash
# 1. 所有容器状态
docker compose -f deploy/compose/docker-compose.prod.yml ps

# 2. 后端 healthz
curl -sk https://localhost/healthz
# 预期: {"code":200,"message":"success","data":{"status":"ok"},...}

# 3. 前端页面
curl -sk https://localhost/ | head -5
# 预期: <!DOCTYPE html>... (React SPA)

# 4. 大屏看板
curl -sk https://localhost/dashboard/ | head -5
# 预期: <!DOCTYPE html>... (Vue3 SPA)

# 5. REST API (需先登录获取 token)
curl -sk https://localhost/api/v1/auth/login \
    -H "Content-Type: application/json" \
    -d '{"username":"admin","password":"password"}'
# 预期: {"code":200,"data":{"access_token":"eyJ...",...}}

# 6. Prometheus 指标
curl -s http://localhost:9090/api/v1/targets | jq '.data.activeTargets[] | {job: .labels.job, health: .health}'
# 预期: mes-backend health=up
```

### 8.2 功能验证

```bash
# 获取 token
TOKEN=$(curl -sk https://localhost/api/v1/auth/login \
    -H "Content-Type: application/json" \
    -d '{"username":"admin","password":"password"}' | jq -r '.data.access_token')

# 验证权限映射 (fail-closed)
curl -sk https://localhost/api/v1/system/users \
    -H "Authorization: Bearer $TOKEN" | jq '.code'
# 预期: 200

# 验证 WebSocket (需 wscat 工具: npm install -g wscat)
wscat -c "wss://localhost/ws/dashboard?token=$TOKEN" --no-check
# 预期: 连接成功, 订阅后收到推送

# 验证 Prometheus 指标
curl -sk https://localhost/metrics | grep mes_http_requests_total
# 预期: 有指标输出
```

### 8.3 默认管理员密码

部署后 **立即修改默认密码**:

```bash
curl -sk https://localhost/api/v1/auth/password \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d '{"old_password":"password","new_password":"你的强密码"}'
```

---

## 9. HTTPS 域名正式配置 (Let's Encrypt)

### 9.1 安装 Certbot

```bash
sudo snap install --classic certbot
sudo ln -s /snap/bin/certbot /usr/local/bin/certbot
```

### 9.2 申请证书

**前提**: 域名 DNS A 记录已指向本服务器 IP, 且 80 端口可从公网访问。

```bash
# 方式 A: standalone 模式 (临时停止 Nginx)
sudo certbot certonly --standalone \
    -d mes.yourcompany.com \
    --email admin@yourcompany.com \
    --agree-tos --no-eff-email

# 方式 B: webroot 模式 (不停服务, 推荐)
sudo mkdir -p /var/www/certbot
# 在 nginx.conf 的 80 端口 server 块中加:
#   location /.well-known/acme-challenge/ {
#       root /var/www/certbot;
#   }
sudo certbot certonly --webroot -w /var/www/certbot \
    -d mes.yourcompany.com \
    --email admin@yourcompany.com \
    --agree-tos --no-eff-email
```

### 9.3 替换证书

```bash
# 备份自签证书
cd /opt/mes/deploy/nginx/certs
cp mes.crt mes.crt.selfsigned
cp mes.key mes.key.selfsigned

# 复制 Let's Encrypt 证书
sudo cp /etc/letsencrypt/live/mes.yourcompany.com/fullchain.pem mes.crt
sudo cp /etc/letsencrypt/live/mes.yourcompany.com/privkey.pem mes.key
sudo chown $(id -u):$(id -g) mes.crt mes.key
chmod 600 mes.key

# 重载 Nginx
docker compose -f /opt/mes/deploy/compose/docker-compose.prod.yml \
    exec nginx nginx -s reload
```

### 9.4 自动续期

```bash
# 测试续期
sudo certbot renew --dry-run

# 添加 Cron 定时任务 (每天凌晨 3 点检查续期)
echo "0 3 * * * certbot renew --quiet --deploy-hook 'docker compose -f /opt/mes/deploy/compose/docker-compose.prod.yml exec nginx nginx -s reload'" | sudo tee /etc/cron.d/certbot-renew
```

### 9.5 更新 Nginx server_name

编辑 `deploy/nginx/nginx.conf`, 将 `server_name _;` 改为实际域名:

```nginx
server {
    listen 443 ssl;
    http2 on;
    server_name mes.yourcompany.com;  # ← 改为实际域名
    # ... 其余不变
}
```

```bash
docker compose -f deploy/compose/docker-compose.prod.yml exec nginx nginx -s reload
```

---

## 10. 可观测性与监控

### 10.1 Prometheus 指标

后端内置 `/metrics` 端点 (公开白名单), 提供以下指标:

| 指标 | 类型 | 说明 |
|------|------|------|
| `mes_http_requests_total{status}` | Counter | HTTP 请求总数 (按状态码) |
| `mes_http_request_duration_ms_bucket` | Histogram | 请求耗时直方图 (9 桶) |
| `mes_outbox_pending` | Gauge | outbox 待投递消息数 |
| `mes_mq_queue_messages{queue}` | Gauge | MQ 队列积压 (passive declare) |
| `mes_partition_days_left{table}` | Gauge | 分区表剩余预建天数 |
| `mes_ws_subscribers` | Gauge | 当前 WS 订阅者数 |
| `mes_ws_broadcast_published_total` | Counter | WS 广播消息总数 |

### 10.2 告警规则

`deploy/prometheus/alerts.yml` 已定义 6 条告警:

| 告警 | 条件 | 严重度 |
|------|------|--------|
| PartitionDaysLeftLow | 分区剩余 < 7 天 | critical |
| MqDataQueueBacklog | iot.data.queue 积压 > 10 万 | warning |
| DlqGrowing | DLQ 10 分钟内新增 > 10 | warning |
| OutboxPendingHigh | outbox 待投递 > 100 | warning |
| HttpP95High | REST P95 > 369ms | warning |
| Http5xxRate | 5xx 比例 > 0.5% | critical |

### 10.3 查看监控

```bash
# Prometheus Web UI
# 浏览器访问: http://服务器IP:9090
# 常用查询:
#   - 请求 QPS:     rate(mes_http_requests_total[5m])
#   - P95 延迟:     histogram_quantile(0.95, sum(rate(mes_http_request_duration_ms_bucket[5m])) by (le))
#   - MQ 积压:      mes_mq_queue_messages
#   - outbox 待投:  mes_outbox_pending

# 告警状态:
#   http://服务器IP:9090/api/v1/alerts
```

> **生产建议**: 接入 Alertmanager 实现邮件/钉钉/企业微信告警推送, 以及 Grafana 做可视化看板。

### 10.4 日志查看

```bash
# 后端日志 (容器内 /app/logs/mes-backend.log)
docker compose -f deploy/compose/docker-compose.prod.yml exec backend \
    tail -f /app/logs/mes-backend.log

# 或直接看容器日志
docker compose -f deploy/compose/docker-compose.prod.yml logs -f backend

# Nginx 访问日志
docker compose -f deploy/compose/docker-compose.prod.yml logs -f nginx

# PostgreSQL 日志
docker compose -f deploy/compose/docker-compose.prod.yml logs -f postgres-primary

# RabbitMQ 日志
docker compose -f deploy/compose/docker-compose.prod.yml logs -f rabbitmq-1
```

---

## 11. 日常运维操作

### 11.1 常用命令速查

```bash
# 进入工作目录
cd /opt/mes/deploy/compose
export COMPOSE_FILE=docker-compose.prod.yml

# 查看所有服务状态
docker compose ps

# 查看资源占用
docker stats --no-stream

# 重启单个服务
docker compose restart backend
docker compose restart nginx

# 查看日志
docker compose logs -f --tail=100 backend

# 进入容器
docker compose exec postgres-primary psql -U mes -d mes
docker compose exec rabbitmq-1 rabbitmqctl status
docker compose exec redis-1 redis-cli -h redis-1 ping   # 单实例; 查内存用 redis-cli -h redis-1 info memory

# 更新镜像后重新部署
docker compose up -d backend   # 滚动更新后端
docker compose up -d nginx     # 更新前端静态文件后
```

### 11.2 数据库操作

```bash
# 连接数据库 (通过 PgBouncer)
docker compose exec pgbouncer psql -U mes -d mes

# 直连 primary (管理操作)
docker compose exec postgres-primary psql -U mes -d mes

# 查看连接数
docker compose exec postgres-primary psql -U mes -d mes -c \
    "SELECT count(*), state FROM pg_stat_activity GROUP BY state;"

# 查看分区状态
docker compose exec postgres-primary psql -U mes -d mes -c \
    "SELECT parent_table, premake, next_partition_creation FROM partman.part_config;"

# 手动触发分区维护
docker compose exec postgres-primary psql -U mes -d mes -c \
    "SELECT partman.run_maintenance_proc();"

# 查看 MQ 队列积压
docker compose exec rabbitmq-1 rabbitmqctl list_queues name messages consumers

# 清空 DLQ (谨慎!)
docker compose exec rabbitmq-1 rabbitmqctl purge_queue iot.dlq
```

### 11.3 前端更新

```bash
cd /opt/mes

# 重新构建前端镜像
docker build -f deploy/web/Dockerfile -t mes-web:latest .
docker build -f deploy/dashboard/Dockerfile -t mes-dashboard:latest .

# 重新提取静态文件
docker run --rm -v "$(pwd)/deploy/compose/web-dist:/out" mes-web:latest \
    sh -c "cp -r /usr/share/nginx/html/* /out/"
docker run --rm -v "$(pwd)/deploy/compose/dashboard-dist:/out" mes-dashboard:latest \
    sh -c "cp -r /usr/share/nginx/html/* /out/"

# 重载 Nginx
docker compose -f deploy/compose/docker-compose.prod.yml exec nginx nginx -s reload
```

---

## 12. 备份与灾备

### 12.1 PostgreSQL 备份

```bash
# 全量逻辑备份
docker compose -f deploy/compose/docker-compose.prod.yml exec -T postgres-primary \
    pg_dump -U mes -d mes --format=custom -f /tmp/mes_backup.dump
docker cp $(docker compose -f deploy/compose/docker-compose.prod.yml ps -q postgres-primary):/tmp/mes_backup.dump \
    /opt/backups/mes_$(date +%Y%m%d_%H%M%S).dump

# 自动备份 (Cron)
echo "0 2 * * * docker compose -f /opt/mes/deploy/compose/docker-compose.prod.yml exec -T postgres-primary pg_dump -U mes -d mes --format=custom | gzip > /opt/backups/mes_$(date +\%Y\%m\%d).dump.gz" | sudo tee /etc/cron.d/mes-pg-backup

# 保留 30 天备份
echo "0 3 * * * find /opt/backups -name 'mes_*.dump.gz' -mtime +30 -delete" | sudo tee -a /etc/cron.d/mes-pg-backup

sudo mkdir -p /opt/backups
```

### 12.2 恢复

```bash
# 恢复到指定备份
docker compose -f deploy/compose/docker-compose.prod.yml exec -T postgres-primary \
    pg_restore -U mes -d mes --clean --if-exists < /opt/backups/mes_20260815.dump
```

### 12.3 Redis 持久化

Redis 单实例默认开启 AOF (`--appendonly yes`), 数据持久化到卷中。无需额外备份配置数据。

### 12.4 配置文件备份

```bash
# 备份所有配置
tar czf /opt/backups/mes_config_$(date +%Y%m%d).tar.gz \
    /opt/mes/deploy/compose/.env \
    /opt/mes/deploy/compose/config-prod/ \
    /opt/mes/deploy/nginx/nginx.conf \
    /opt/mes/deploy/nginx/certs/
```

---

## 13. 蓝绿发布与回滚

### 13.1 蓝绿发布流程

项目已内置蓝绿发布支持 (`scripts/release_drill.ps1` 为 Windows 版编排, Linux 下手动操作):

```bash
cd /opt/mes/deploy/compose

# 1. Expand: 启动新版实例 (不同端口, 如 8090)
# 修改 drogon_config.c.json 指向生产 DB, 启动临时容器
docker run -d --name mes-backend-green \
    --network compose_default \
    -v "$(pwd)/config-prod:/app/config:ro" \
    mes-backend:latest \
    config/drogon_config.json

# 2. Nginx 灰度: 修改 nginx.conf, 用 split_clients 分流 10% 到 green
# 参考 deploy/nginx/nginx.drill.conf

# 3. 观察 Prometheus 指标和日志, 确认 green 正常

# 4. 全量切换: 修改 upstream 指向 green, reload nginx

# 5. Contract: 停止旧版 (blue)
docker stop mes-backend-green-old
```

### 13.2 快速回滚

```bash
# 回滚到旧版镜像
cd /opt/mes/deploy/compose
docker compose stop backend
MES_VERSION=<旧版本号> docker compose up -d backend
```

### 13.3 配置热重载

```bash
# Nginx 配置热重载 (不中断连接)
docker compose exec nginx nginx -s reload

# 后端配置变更需重启
docker compose restart backend
```

---

## 14. 故障排查

### 14.1 常见问题

| 现象 | 可能原因 | 解决方案 |
|------|---------|---------|
| 后端启动失败, 日志报 DB 连接超时 | PgBouncer 未就绪或密码不匹配 | 检查 `config-prod/drogon_config.json` 中 host=pgbouncer, passwd 与 .env 一致 |
| 后端报 `incorrect binary data format` | 数值绑定参数未用 SqlArg | 代码问题, 见 HANDOVER.md 踩坑 #19 |
| Nginx 502 Bad Gateway | 后端未启动或端口不匹配 | `docker compose ps backend`; 检查 nginx.conf upstream |
| WebSocket 连接失败 | Nginx WSS 代理配置缺失 | 检查 nginx.conf `/ws` location 有 Upgrade/Connection 头 |
| Redis 不可用 | 单实例进程异常或 OOM 被杀 | `docker compose exec redis-1 redis-cli -h redis-1 ping`; 检查 `docker logs redis-1` 与内存上限 |
| RabbitMQ `CONNECTION_REFUSED` | RMQ 集群未组建 | 检查 Erlang Cookie 一致; `rabbitmqctl cluster_status` |
| PG 分区写入失败 `no partition` | pg_partman 未预建足够分区 | `SELECT partman.run_maintenance_proc();` 手动触发 |
| 后端 OOM 或高 CPU | 连接池/线程数过大 | 降低 `connection_number` 或 `threads_num` |
| 前端登录返回 CORS 错误 | 前端直连后端而非通过 Nginx | 前端必须同源接入 (Nginx 反代 /api) |

### 14.2 诊断命令

```bash
# 全面健康检查
docker compose -f deploy/compose/docker-compose.prod.yml ps
docker compose -f deploy/compose/docker-compose.prod.yml top

# 网络连通性
docker compose -f deploy/compose/docker-compose.prod.yml exec backend \
    sh -c "nc -zv pgbouncer 6432 && nc -zv redis-1 6379 && nc -zv rabbitmq-1 5672"

# 磁盘空间
df -h
docker system df

# 清理无用镜像 (不清理运行中的容器卷)
docker system prune -f

# PG 锁等待
docker compose -f deploy/compose/docker-compose.prod.yml exec postgres-primary \
    psql -U mes -d mes -c \
    "SELECT pid, state, wait_event_type, wait_event, query FROM pg_stat_activity WHERE state != 'idle';"
```

### 14.3 获取帮助

| 资源 | 位置 |
|------|------|
| 架构设计文档 | `docs/MES_Aarchiture_Design.md` |
| 交接文档 | `HANDOVER.md` |
| 构建进度与踩坑 | `docs/BUILD_PROGRESS.md` |
| 贡献指南 | `CONTRIBUTING.md` |
| 踩坑清单 (43 条) | `HANDOVER.md` 第七节 |

---

## 附录 A: 完整部署 Checklist

```
□ 1. Ubuntu 24.04 系统安装完成, SSH 可达
□ 2. Docker Engine + Compose + migrate + certbot 安装完成
□ 3. 防火墙配置 (22/80/443 开放)
□ 4. 系统参数调优 (文件描述符/TCP)
□ 5. 源码克隆到 /opt/mes
□ 6. 后端镜像构建成功 (docker build)
□ 7. 前端镜像构建成功 (web + dashboard)
□ 8. 前端静态文件提取到 deploy/compose/web-dist 和 dashboard-dist
□ 9. 定制 PG 镜像构建成功
□ 10. .env 文件配置 (PG/MQ 密码, Cookie)
□ 11. config-prod/drogon_config.json 配置 (PG密码, JWT密钥)
□ 12. config-prod/rabbitmq.json 配置 (MQ密码)
□ 13. TLS 证书生成 (自签或 Let's Encrypt)
□ 14. 中间件启动 (PG + PgBouncer + Redis 单实例 + RMQ)
□ 15. 只读副本初始化 (init_replica.sh)
□ 16. 数据库迁移执行成功 (migrate up)
□ 17. 分区和定时任务验证 (partman + cron)
□ 18. 后端启动 + healthz 通过
□ 19. Nginx 启动 + HTTPS 可访问
□ 20. Prometheus 启动 + 指标可查
□ 21. 功能验证 (登录 + API + WS + 前端页面)
□ 22. 默认密码修改
□ 23. 备份计划配置 (Cron + pg_dump)
□ 24. (可选) Let's Encrypt 正式证书 + 自动续期
□ 25. (可选) Alertmanager + Grafana 接入
```

---

## 附录 B: 端口映射参考

| 端口 | 服务 | 对外 | 说明 |
|------|------|------|------|
| 80 | Nginx | 是 | HTTP → 301 HTTPS |
| 443 | Nginx | 是 | HTTPS + WSS 入口 |
| 5432 | PostgreSQL | 否 (内部) | 主库 (通过 PgBouncer 访问) |
| 6432 | PgBouncer | 仅 127.0.0.1 | 连接池入口 (Docker 发布端口绕过 ufw, 故显式绑本机) |
| 6379 | Redis | 否 (内部) | Redis 单实例 |
| 5672 | RabbitMQ AMQP | 否 (内部) | 消息队列 |
| 15672 | RabbitMQ Management | 否 (内部) | 管理界面 (需 SSH 隧道) |
| 8088 | Backend | 否 (内部) | Drogon HTTP/WS |
| 9090 | Prometheus | 可选 | 监控界面 |

---

## 附录 C: 开发环境快速部署 (docker-compose.dev.yml)

开发环境用于本地调试, 简化为单实例中间件:

```bash
cd /opt/mes

# 启动开发中间件 (PG 单实例 + Redis 单实例 + RMQ 单节点)
docker compose -f deploy/compose/docker-compose.dev.yml up -d --build

# 运行迁移
migrate -path mes-backend/migrations \
    -database "postgres://mes:mes_dev_pwd@localhost:5432/mes?sslmode=disable" up

# 后端编译 (需 vcpkg + CMake)
cmake -S mes-backend -B mes-backend/build \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build mes-backend/build -j

# 启动后端
./mes-backend/build/mes-backend mes-backend/config/drogon_config.json

# 前端开发服务器
cd mes-web && npm install && npm run dev      # → http://localhost:5173
cd mes-dashboard && npm install && npm run dev # → http://localhost:5174
```

开发环境默认凭据: `admin / password`
开发环境连接串:
- PG: `postgres://mes:mes_dev_pwd@localhost:5432/mes`
- Redis: `localhost:6379`
- RabbitMQ: `mes/mes_dev_pwd@localhost:5672` (管理台 15672)

---

> **文档版本**: 1.0 | **最后更新**: 2026-08-15 | **维护者**: MES 团队
