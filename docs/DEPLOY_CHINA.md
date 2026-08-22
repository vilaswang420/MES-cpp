# MES 后端镜像 - 国内环境部署文档（阿里云实测）

> 版本: 1.1 | 更新: 2026-08-20 | 适用: 阿里云/腾讯云/华为云等国内服务器
>
> **核心策略：宿主机完成全部编译，Docker 只打包。**
> 彻底避开 Docker 构建时的 GitHub/vcpkg 网络问题，构建镜像 100% 无境外访问。

---

## 原理：为什么不在 Docker 里编译？

| 环节 | 在 Docker 里编译 | 宿主机编译 + Docker 打包 |
|------|------------------|--------------------------|
| 基础镜像 | 需拉 ubuntu（可能失败） | 本地缓存即可 ✅ |
| vcpkg 克隆 | 需访问 GitHub（被墙） | Gitee 镜像 ✅ |
| vcpkg 自举 | 下载 vcpkg-glibc（被墙） | `VCPKG_FORCE_SYSTEM_BINARIES=1` ✅ |
| 依赖源码下载 | 访问 GitHub（被墙） | `ghfast.top` 代理 ✅ |
| 编译耗时 | 30-60 分钟（每层拉取） | 一次性，之后直接复用 ✅ |
| 二进制打包 | - | 只 COPY，秒级完成 ✅ |

---

## 1. 宿主机编译（一次性，约 30-60 分钟）

```bash
cd /opt/mes/MES-cpp
bash scripts/build_cn.sh
```

脚本自动完成：
1. 安装工具链（阿里云 apt）
2. 从 **Gitee 镜像**完整克隆 vcpkg
3. 用系统 gcc 自举 vcpkg（`VCPKG_FORCE_SYSTEM_BINARIES=1`，不下载 vcpkg-glibc）
4. 配置 vcpkg 走 **ghfast.top** GitHub 代理下载依赖源码
5. cmake 编译 → 产出 `build-out/mes-backend`

> **首次会失败的话**：若 `ghfast.top` 也不通，在能联网的机器（如你本地 Windows + WARP）手动把依赖源码下载好放缓存，见 [第 4 节](#4-备选依赖源码缓存方案)。

---

## 2. 打包镜像（无需外网）

```bash
cd /opt/mes/MES-cpp
docker build -f deploy/backend/Dockerfile.cn -t mes-backend:latest .
```

`deploy/backend/Dockerfile.cn` 做的事：
- `FROM ubuntu:24.04`（本地缓存，已验证可拉）
- 切阿里云 apt 源，装运行时库 `libssl3 zlib1g ca-certificates tzdata`
- COPY 宿主机编好的二进制 + 配置 + 建日志/上传目录
- **全程无 git clone、无 curl 下载、无 vcpkg**

构建日志预期（应很快）：
```
=> [internal] load build definition ... 0.0s
=> [1/5] FROM ubuntu:24.04 ...          0.0s (CACHED)
=> [2/5] RUN apt-get update && ...      若干秒 (阿里云源)
=> [3/5] COPY build-out/mes-backend ... 0.0s
=> exporting to image ...                秒级
```

---

## 3. 验证

```bash
# 二进制存在
docker run --rm mes-backend:latest ls -lh /app/mes-backend

# 冒烟测试 (需先有中间件)
docker compose -f deploy/compose/docker-compose.prod.yml up -d \
    postgres-primary pgbouncer redis-1 redis-2 redis-3 \
    redis-4 redis-5 redis-6 redis-cluster-init \
    rabbitmq-1 rabbitmq-2 rabbitmq-3
```

---

## 4. 备选：依赖源码缓存方案

若服务器访问 `ghfast.top` 也不稳定，在**能联网的机器**（本地 Windows + WARP）预下载依赖源码，再上传到服务器：

```bash
# 本地 Windows (有网):
#   1. git clone vcpkg (GitHub 或 gitee 均可)
#   2. 进入 vcpkg 目录执行 (会下载 drogon/hiredis/jwt-cpp 等全部源码到 downloads/)
#      vcpkg install --x-manifest-root=<项目路径>/mes-backend

# 上传 downloads 缓存到服务器
scp -r <vcpkg目录>/downloads root@服务器:/opt/vcpkg/downloads

# 服务器上重新跑
bash scripts/build_cn.sh   # vcpkg 已存在则跳过克隆, 直接复用缓存编译
```

---

## 5. 前端 / PG 镜像国内化（同样思路）

### 前端镜像（`deploy/web/Dockerfile`、`deploy/dashboard/Dockerfile`）
```dockerfile
FROM docker.mirrors.ustc.edu.cn/library/node:20-alpine AS build
RUN npm config set registry https://registry.npmmirror.com
WORKDIR /app
COPY mes-web/ mes-web/
RUN cd mes-web && npm ci && npm run build
# ...
```
> 若 USTC 镜像域名解析失败，换 `registry.cn-hangzhou.aliyuncs.com/library/node:20-alpine`（阿里云需登录）或配置 Docker daemon 镜像加速器。

### 定制 PG 镜像（`deploy/postgres/Dockerfile`）
```dockerfile
FROM postgres:16
# 原 Dockerfile 已内置 ghfast.top 回退:
#   (curl github 直连失败时, 自动改用 https://ghfast.top/https://github.com/...)
# 无需改动, 直接构建
docker build -f deploy/postgres/Dockerfile -t mes-postgres:16 deploy/postgres/
```

---

## 6. Docker daemon 镜像加速（可选，建议配置）

```bash
sudo tee /etc/docker/daemon.json <<'EOF'
{
  "registry-mirrors": [
    "https://docker.mirrors.ustc.edu.cn",
    "https://hub-mirror.c.163.com",
    "https://mirror.baidubce.com"
  ]
}
EOF
sudo systemctl daemon-reload && sudo systemctl restart docker
```

> 若这些镜像域名解析失败，请检查 `/etc/resolv.conf`（改用 `223.5.5.5`、`114.114.114.114`）后重启 Docker。

---

## 7. 常见问题

| 现象 | 解决 |
|------|------|
| `git clone gitee.com/mirrors/vcpkg` 失败 | 检查 DNS：`echo nameserver 223.5.5.5 > /etc/resolv.conf` 后重试 |
| vcpkg bootstrap 卡在下载 vcpkg-glibc | 确保脚本里设了 `VCPKG_FORCE_SYSTEM_BINARIES=1`（脚本已内置） |
| vcpkg install 下载依赖源码超时 | 用第 4 节预下载缓存方案 |
| Dockerfile.cn 打包缺 `build-out/mes-backend` | 先执行 `bash scripts/build_cn.sh` 成功 |
| 运行时缺动态库 | Dockerfile.cn 已装 `libssl3 zlib1g`，若缺其他，`ldd /app/mes-backend` 查看 |
| `COPY` 找不到文件 | 确认在仓库根目录执行 docker build（构建上下文 = 根目录） |

---

## 8. 完整流程速查

```bash
# 一次性: 宿主机编译 (30-60 min)
cd /opt/mes/MES-cpp && bash scripts/build_cn.sh

# 打包 (秒级)
docker build -f deploy/backend/Dockerfile.cn -t mes-backend:latest .

# 后续代码更新只需重编 + 重打包
git pull
bash scripts/build_cn.sh
docker build -f deploy/backend/Dockerfile.cn -t mes-backend:latest .
```

---

> **核心原则**：Docker 构建阶段不出现任何 `github.com`/`raw.githubusercontent.com`/`pkg.cloudflare.com`/`download.docker.com`。所有需要境外网络的动作都收敛到宿主机一次性完成。