# hms-iot 采集服务镜像 (P4-5.1)
# 多阶段构建: vcpkg → 编译 → 运行镜像
# 构建: docker build -f deploy/iot/Dockerfile -t hms-iot:<version> .
# 构建上下文 = 仓库根目录

# ---- 阶段 1: 编译 ----
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates \
        zip unzip tar pkg-config \
    && rm -rf /var/lib/apt/lists/*

# vcpkg (manifest 模式, 依赖 = hms-iot/vcpkg.json)
RUN git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg

WORKDIR /src
COPY hms-iot/ hms-iot/

# 依赖编译单独分层缓存
RUN cmake -S hms-iot -B hms-iot/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build hms-iot/build -j"$(nproc)"

# ---- 阶段 2: 运行 ----
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates tzdata \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/hms-iot/build/hms-iot ./hms-iot
# 配置以挂载为准 (compose 覆盖 config/iot.json)
COPY hms-iot/config/iot.json /app/config/iot.json
EXPOSE 8091
HEALTHCHECK --interval=10s --timeout=3s --retries=6 \
    CMD ["sh", "-c", "exec 3<>/dev/tcp/127.0.0.1/8091 && printf 'GET /healthz HTTP/1.0\\r\\n\\r\\n' >&3 && grep -q ok <&3"]
CMD ["./hms-iot", "config/iot.json"]
