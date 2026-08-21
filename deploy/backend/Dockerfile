# mes-backend 生产镜像 (Linux/Ubuntu 部署, 见 docs/DEPLOY_LINUX.md)
# 首次构建需 vcpkg 编译全部 C++ 依赖 (约 30-60 分钟, 视网速), 之后有 BuildKit 缓存会快很多。
# 构建: docker build -f deploy/backend/Dockerfile -t mes-backend:<version> .
# 注意: 构建上下文 = 仓库根目录

# ---- 阶段 1: 编译 ----
# 说明: 本文件为「海外/标准网络」构建路径 (Docker 内两阶段编译, 直连 github.com / postgresql.org)。
#       国内网络 (阿里云等) 请改用 Dockerfile.cn + scripts/build_cn.sh (宿主预编译, 走国内镜像/代理)。
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc-14 g++-14 cmake ninja-build git curl ca-certificates \
        zip unzip tar pkg-config libssl-dev zlib1g-dev \
        python3-pip perl nasm bison flex \
    && rm -rf /var/lib/apt/lists/*

# 固定 gcc-14: GCC 13 编译 drogon::Task<> C++20 协程会触发 internal compiler error (build_special_member_call), gcc-14 已修复
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 60 \
 && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 60

# vcpkg 自举工具版本要求: cmake>=4.4.0 / ninja>=1.13.2 (apt 仅 3.28/1.11 过低)
#   - cmake 走 pip (pypi 直连)
#   - ninja 直接下载官方 1.13.2 到 /usr/local/bin (优先于 apt 的 1.11)
#   - VCPKG_FORCE_SYSTEM_BINARIES=1: 强制 vcpkg 用系统工具, 不再自举下载副本并注入 PATH (避免 Host path separator 报错)
RUN pip3 install --break-system-packages cmake \
 && curl -fL https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip -o /tmp/ninja.zip \
 && unzip -o /tmp/ninja.zip -d /usr/local/bin/ && chmod +x /usr/local/bin/ninja \
 && rm -f /tmp/ninja.zip
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

# vcpkg (锁定 manifest 模式, 依赖清单 = mes-backend/vcpkg.json)
RUN git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg

WORKDIR /src
COPY mes-backend/ mes-backend/

# 依赖编译单独分层缓存 (vcpkg_installed 很大, 变更依赖才重建)
#   - 必须 dynamic triplet: simpleamqpclient 等 port 仅支持动态链接 ("!static"), 静态 x64-linux 会失败
#   - 固定 gcc-14: 避免 GCC 13 协程 ICE
RUN cmake -S mes-backend -B mes-backend/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
        -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build mes-backend/build -j"$(nproc)"

# ---- 阶段 2: 运行 ----
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 zlib1g ca-certificates tzdata \
        libkrb5-3 libldap2 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
# 动态链接产物: 打包 vcpkg 依赖的 .so (libSimpleAmqpClient / libpq / libhiredis / libdrogon ...) 供加载器查找
COPY --from=build /src/mes-backend/build/vcpkg_installed/x64-linux-dynamic/lib/*.so* /app/lib/
ENV LD_LIBRARY_PATH=/app/lib
COPY --from=build /src/mes-backend/build/mes-backend ./mes-backend
# 配置以挂载为准 (compose: ./config-prod -> /app/config), 镜像内只放兜底
COPY mes-backend/config/drogon_config.prod.json /app/config/drogon_config.json
COPY mes-backend/config/rabbitmq.json /app/config/rabbitmq.json
RUN mkdir -p /app/logs /app/uploads
EXPOSE 8088
HEALTHCHECK --interval=10s --timeout=3s --retries=6 \
    CMD ["sh", "-c", "exec 3<>/dev/tcp/127.0.0.1/8088 && printf 'GET /healthz HTTP/1.0\\r\\n\\r\\n' >&3 && grep -q ok <&3"]
CMD ["./mes-backend", "config/drogon_config.json"]
