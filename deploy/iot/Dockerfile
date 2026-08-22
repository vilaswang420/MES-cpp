# mes-iot 采集服务镜像 (P4-5.1)
# 多阶段构建: vcpkg → 编译 → 运行镜像
# 构建: docker build -f deploy/iot/Dockerfile -t mes-iot:<version> .
# 构建上下文 = 仓库根目录
#
# 国内构建请使用 Dockerfile.cn (宿主机编译 + 离线打包, 无网络依赖)。
# 本 Dockerfile 适用于有 GitHub 访问能力的环境 (CI/CD, 海外服务器)。

# ---- 阶段 1: 编译 ----
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive

# 切换阿里云 apt 源 (国内构建加速; 海外可删除此段)
RUN sed -i 's|http://archive.ubuntu.com/ubuntu/|http://mirrors.aliyun.com/ubuntu/|g; s|http://security.ubuntu.com/ubuntu/|http://mirrors.aliyun.com/ubuntu/|g' /etc/apt/sources.list.d/ubuntu.sources \
 && apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc-14 g++-14 \
        cmake ninja-build git curl ca-certificates \
        zip unzip tar pkg-config \
    && rm -rf /var/lib/apt/lists/*

# vcpkg (manifest 模式, 依赖 = mes-iot/vcpkg.json)
# 注意: --depth 1 克隆最新代码, 如需版本锁定请在 vcpkg.json 中添加 builtin-baseline
RUN git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg
# 强制使用系统已安装的 cmake/ninja, 避免 vcpkg 自举下载工具时
# 触发 "Host path separator (:) in path" 报错
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

WORKDIR /src
COPY mes-iot/ mes-iot/

# 依赖编译单独分层缓存
# 关键配置:
#   - VCPKG_TARGET_TRIPLET=x64-linux-dynamic: simpleamqpclient port 仅支持动态链接 (!static),
#     默认 x64-linux (静态) 会报 "does not match x64-linux"
#   - CMAKE_C/CXX_COMPILER=gcc-14/g++-14: GCC 13 在 C++20 协程下存在 ICE bug
#     (internal compiler error: in build_special_member_call, at cp/call.cc:11096)
RUN cmake -S mes-iot -B mes-iot/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
        -DCMAKE_C_COMPILER=gcc-14 \
        -DCMAKE_CXX_COMPILER=g++-14 \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build mes-iot/build -j"$(nproc)"

# ---- 阶段 2: 运行 ----
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# 切换阿里云 apt 源 + 安装运行时动态库依赖
# 动态链接产物需要: libssl3, zlib1g (openssl/boost 运行时)
# libkrb5-3, libldap2: libpq 客户端库运行时可能动态链接
RUN sed -i 's|http://archive.ubuntu.com/ubuntu/|http://mirrors.aliyun.com/ubuntu/|g; s|http://security.ubuntu.com/ubuntu/|http://mirrors.aliyun.com/ubuntu/|g' /etc/apt/sources.list.d/ubuntu.sources \
 && apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates tzdata \
        libssl3 zlib1g \
        libkrb5-3 libldap2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/mes-iot/build/mes-iot ./mes-iot
# 动态链接产物: 一并打包 vcpkg 依赖的 .so (LD_LIBRARY_PATH 让加载器全局可寻, 含传递依赖)
COPY --from=build /src/mes-iot/build/vcpkg_installed/x64-linux-dynamic/lib/*.so* /app/lib/
ENV LD_LIBRARY_PATH=/app/lib
# 配置以挂载为准 (compose 覆盖 config/iot.json)
COPY mes-iot/config/iot.json /app/config/iot.json

RUN mkdir -p /app/logs

EXPOSE 8091
HEALTHCHECK --interval=10s --timeout=3s --retries=6 \
    CMD ["sh", "-c", "exec 3<>/dev/tcp/127.0.0.1/8091 && printf 'GET /healthz HTTP/1.0\\r\\n\\r\\n' >&3 && grep -q ok <&3"]
CMD ["./mes-iot", "config/iot.json"]
