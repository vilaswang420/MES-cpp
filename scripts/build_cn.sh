#!/bin/bash
# ===== mes-backend 国内宿主机编译脚本 (一次性) =====
# 在阿里云服务器上执行: bash scripts/build_cn.sh
# 产出: build-out/mes-backend 二进制 (供 Dockerfile.cn 打包)
# 国内网络适配:
#   1. vcpkg 仓库  -> Gitee 镜像 (国内可达)
#   2. vcpkg 自举   -> VCPKG_FORCE_SYSTEM_BINARIES=1 (用系统 gcc, 不下载 vcpkg-glibc)
#   3. 依赖源码下载 -> 走 ghfast.top GitHub 代理 (仅镜像 github.com)
#   4. 非 GitHub 源 -> postgresql.org 等走各自国内镜像 seed 到 vcpkg downloads (ghfast.top 覆盖不到)
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> [1/5] 安装编译工具链 (阿里云 apt)"
# 注: perl/nasm (openssl 端口编译需) + bison/flex (libpq/postgresql 端口生成 SQL 解析器需)
# 非 vcpkg 版本门槛, 但 vcpkg 从源码编译依赖时必须在 PATH 中可找到, 缺失会中途 command not found.
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git curl ca-certificates \
    python3-pip zip unzip tar libssl-dev zlib1g-dev \
    perl nasm bison flex \
    gcc-14 g++-14

echo "==> [1.5/5] 确保 cmake >= 4.4.0 (vcpkg 自举新要求, 系统 apt 仅 3.28 过低)"
# vcpkg 近期版本要求 cmake >= 4.4.0, 而 noble apt 仓库最高只有 3.28.3,
# 导致 vcpkg 试图从 github 自举下载 cmake -> 国内直连 github 失败 (curl 56).
# 解决: 优先 pip 安装 (走 pypi 镜像, 不经 GitHub); 失败再走 ghfast.top 代理下载官方预编译包.
ensure_cmake() {
  local REQ="4.4.0"
  local HAVE
  HAVE=$(cmake --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)
  if [ -n "$HAVE" ] && printf '%s\n%s\n' "$REQ" "$HAVE" | sort -V -C 2>/dev/null; then
    echo "    已满足: cmake $HAVE"
    return 0
  fi
  echo "    当前 cmake ($HAVE) < $REQ, 升级中 ..."
  # 方案A: pip (走 pypi 镜像, 不经 GitHub)
  if command -v pip3 >/dev/null 2>&1; then
    pip3 install --break-system-packages cmake >/dev/null 2>&1 && hash -r
    local NEW
    NEW=$(cmake --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)
    if [ -n "$NEW" ] && printf '%s\n%s\n' "$REQ" "$NEW" | sort -V -C 2>/dev/null; then
      echo "    已通过 pip 安装 cmake $NEW"
      return 0
    fi
  fi
  # 方案B: ghfast.top 代理下载官方预编译包 (覆盖 vcpkg 自举不走资产代理的缺口)
  echo "    pip 失败, 改从 ghfast.top 下载官方 cmake 预编译包 ..."
  local CM_URL="https://github.com/Kitware/CMake/releases/download/v${REQ}/cmake-${REQ}-linux-x86_64.tar.gz"
  curl -fL "https://ghfast.top/${CM_URL}" -o /tmp/cmake.tar.gz \
    || curl -fL "$CM_URL" -o /tmp/cmake.tar.gz
  sudo rm -rf "/opt/cmake-${REQ}"
  sudo tar -xzf /tmp/cmake.tar.gz -C /opt
  export PATH="/opt/cmake-${REQ}/bin:$PATH"
  hash -r
  echo "    已安装 cmake $(cmake --version | head -1 | grep -oE '[0-9.]+') 到 /opt/cmake-${REQ}"
}

# 确保 ninja >= 1.13.2 (vcpkg 自举新要求, 系统 apt 仅 1.11.1 过低)
# 若系统已满足, vcpkg 直接复用, 不会去 github 下载并触发 add_to_path 的
# "Host path separator" 报错.
ensure_ninja() {
  local REQ="1.13.2"
  local HAVE
  HAVE=$(ninja --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)
  if [ -n "$HAVE" ] && printf '%s\n%s\n' "$REQ" "$HAVE" | sort -V -C 2>/dev/null; then
    echo "    已满足: ninja $HAVE"
    return 0
  fi
  echo "    当前 ninja ($HAVE) < $REQ, 升级中 ..."
  # 方案A: pip (走 pypi 镜像, 不经 GitHub)
  if command -v pip3 >/dev/null 2>&1; then
    pip3 install --break-system-packages ninja >/dev/null 2>&1 && hash -r
    local NEW
    NEW=$(ninja --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)
    if [ -n "$NEW" ] && printf '%s\n%s\n' "$REQ" "$NEW" | sort -V -C 2>/dev/null; then
      echo "    已通过 pip 安装 ninja $NEW"
      return 0
    fi
  fi
  # 方案B: ghfast.top 代理下载官方预编译包 (覆盖 vcpkg 自举不走资产代理的缺口)
  echo "    pip 失败, 改从 ghfast.top 下载官方 ninja 预编译包 ..."
  curl -fL "https://ghfast.top/https://github.com/ninja-build/ninja/releases/download/v${REQ}/ninja-linux.zip" -o /tmp/ninja.zip \
    || curl -fL "https://github.com/ninja-build/ninja/releases/download/v${REQ}/ninja-linux.zip" -o /tmp/ninja.zip
  sudo unzip -o /tmp/ninja.zip -d /usr/local/bin/
  sudo chmod +x /usr/local/bin/ninja
  hash -r
  echo "    已安装 ninja $(ninja --version) 到 /usr/local/bin"
}
ensure_ninja

ensure_cmake

echo "==> [2/5] 准备 vcpkg"
VCPKG_ROOT="${VCPKG_ROOT:-/opt/vcpkg}"
if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
    sudo rm -rf "$VCPKG_ROOT"
    sudo git clone https://gitee.com/mirrors/vcpkg.git "$VCPKG_ROOT"
    sudo chown -R "$USER:$USER" "$VCPKG_ROOT"
    # 系统 gcc 编译 vcpkg 本体, 绕过 vcpkg-glibc 的 GitHub 下载
    (cd "$VCPKG_ROOT" && VCPKG_FORCE_SYSTEM_BINARIES=1 ./bootstrap-vcpkg.sh -disableMetrics)
else
    echo "    vcpkg 已存在: $VCPKG_ROOT (跳过克隆)"
fi
export VCPKG_ROOT

echo "==> [2.5/5] 固定编译器为 gcc-14 (GCC 13 在 C++20 协程下会 internal compiler error)"
# GCC 13 编译 drogon::Task<> 协程时触发 build_special_member_call 内部编译器错误 (ICE),
# 升级到 gcc-14 后修复。noble apt 自带 gcc-14, 设为 C/C++ 编译器供 vcpkg 端口与 mes-backend 共用。
export CC=gcc-14
export CXX=g++-14

echo "==> [3/5] 配置 vcpkg 资产代理 (ghfast.top)"
# 让 vcpkg 下载依赖源码时走 GitHub 代理; 本地已有 downloads 缓存则不受影响
cat > "$VCPKG_ROOT/vcpkg-configuration.json" <<EOF
{
  "assetCache": {
    "mirror": {
      "kind": "url",
      "url": "https://ghfast.top/",
      "expiration": "30d"
    }
  }
}
EOF

echo "==> [3.5/5] 预置非 GitHub 源码 (ghfast.top 仅镜像 github.com, 覆盖不到的源需国内镜像 seed)"
# 问题: libpq 端口依赖 postgresql 源码 (postgresql-18.4.tar.bz2) 从 ftp.postgresql.org 下载,
#   ghfast.top 代理只镜像 github.com, 国内直连 postgresql.org 会卡死/超时 (实测 tuna/ustc/华为云均 404,
#   仅 aliyun/tencent 镜像可用, 而服务器在阿里云故优先 aliyun)。
# 解决: 构建前先用对应国内镜像把 tarball 落到 vcpkg downloads 缓存, vcpkg 校验 SHA512 通过后会
#   直接复用, 不再直连上游源。键=下载目标文件名(vcpkg downloads 里的 base name), 值=国内镜像完整 URL。
seed_non_github_sources() {
  local DL="$VCPKG_ROOT/downloads"
  mkdir -p "$DL"
  declare -A SEED=(
    ["postgresql-18.4.tar.bz2"]="https://mirrors.aliyun.com/postgresql/source/v18.4/postgresql-18.4.tar.bz2"
  )
  for f in "${!SEED[@]}"; do
    if [ -s "$DL/$f" ]; then
      echo "    已存在(跳过): $f"
      continue
    fi
    echo "    seed 非GitHub源: $f"
    curl -fL "${SEED[$f]}" -o "$DL/$f" \
      || echo "    WARN: $f 下载失败, vcpkg 将尝试直连上游(可能超时)"
  done
}
seed_non_github_sources

echo "==> [4/5] vcpkg install (manifest 模式, 依赖清单 = mes-backend/vcpkg.json)"
# 关键: 强制 vcpkg 使用系统已装好的 cmake(>=4.4.2)/ninja(>=1.13.2),
# 不要自举下载自带副本并 add_to_path -- 否则在部分环境会触发
# "Host path separator (:) in path" 报错 (vcpkg relocate 工具时路径分隔符冲突)。
# 注意: 必须确保系统 ninja 真的是 >=1.13.2 (pip 镜像最高仅 1.13.0, 需用 ghfast.top 装官方 1.13.2)。
export VCPKG_FORCE_SYSTEM_BINARIES=1
unset VCPKG_HOST_PATH_LIST_SEPARATOR
# 必须用动态 triplet: simpleamqpclient 等 port 仅支持动态链接 ("!static"),
# vcpkg 默认 x64-linux 是静态 triplet, 会触发 "does not match x64-linux" 报错。
# 注: VCPKG_TARGET_TRIPLET 环境变量被 vcpkg.cmake 忽略, 必须走 -D (同 CI 配置)。
export VCPKG_BINARY_SOURCES="clear;files,$PWD/build-out/vcpkg-cache,readwrite"
mkdir -p build-out/vcpkg-cache
cmake -S mes-backend -B build-out/cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
    -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

echo "==> [5/5] 编译二进制"
cmake --build build-out/cmake -j"$(nproc)"
mkdir -p build-out
cp -f build-out/cmake/mes-backend build-out/mes-backend
chmod +x build-out/mes-backend

echo ""
echo "完成! 二进制: $PWD/build-out/mes-backend"
echo "接下来打包镜像:"
echo "  docker build -f deploy/backend/Dockerfile.cn -t mes-backend:latest ."