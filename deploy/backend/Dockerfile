# hms-backend 生产镜像 (Linux/Ubuntu 部署, 见 docs/DEPLOY_LINUX.md)
# 首次构建需 vcpkg 编译全部 C++ 依赖 (约 30-60 分钟, 视网速), 之后有 BuildKit 缓存会快很多。
# 构建: docker build -f deploy/backend/Dockerfile -t hms-backend:<version> .
# 注意: 构建上下文 = 仓库根目录

# ---- 阶段 1: 编译 ----
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates \
        zip unzip tar pkg-config libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# vcpkg (锁定 manifest 模式, 依赖清单 = hms-backend/vcpkg.json)
RUN git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg

WORKDIR /src
COPY hms-backend/ hms-backend/

# 依赖编译单独分层缓存 (vcpkg_installed 很大, 变更依赖才重建)
RUN cmake -S hms-backend -B hms-backend/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build hms-backend/build -j"$(nproc)"

# ---- 阶段 2: 运行 ----
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 zlib1g ca-certificates tzdata \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/hms-backend/build/hms-backend ./hms-backend
# 配置以挂载为准 (compose: ./config-prod -> /app/config), 镜像内只放兜底
COPY hms-backend/config/drogon_config.prod.json /app/config/drogon_config.json
COPY hms-backend/config/rabbitmq.json /app/config/rabbitmq.json
RUN mkdir -p /app/logs /app/uploads
EXPOSE 8088
HEALTHCHECK --interval=10s --timeout=3s --retries=6 \
    CMD ["sh", "-c", "exec 3<>/dev/tcp/127.0.0.1/8088 && printf 'GET /healthz HTTP/1.0\\r\\n\\r\\n' >&3 && grep -q ok <&3"]
CMD ["./hms-backend", "config/drogon_config.json"]
