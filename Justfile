# justfile — HMS 统一命令入口 (安装: https://github.com/casey/just)
set shell := ["powershell", "-NoProfile", "-Command"]
set dotenv-load := false

root := justfile_directory()
compose_dev := root + "/deploy/compose/docker-compose.dev.yml"

# 一键开发环境: 起中间件 -> 迁移 -> 四服务 healthz (M0 DoD)
dev-up:
    just infra-up
    just migrate-up
    @echo "infra ready; start hms-backend/hms-iot/hms-web/hms-dashboard manually or via their own scripts"

infra-up:
    docker compose -f "{{compose_dev}}" up -d --build

infra-down:
    docker compose -f "{{compose_dev}}" down

infra-logs *args:
    docker compose -f "{{compose_dev}}" logs -f {{args}}

# golang-migrate (需安装 migrate CLI)
migrate-up:
    migrate -path "{{root}}/hms-backend/migrations" -database "postgres://hms:hms_dev_pwd@localhost:5432/hms?sslmode=disable" up

migrate-down:
    migrate -path "{{root}}/hms-backend/migrations" -database "postgres://hms:hms_dev_pwd@localhost:5432/hms?sslmode=disable" down 1

migrate-force version:
    migrate -path "{{root}}/hms-backend/migrations" -database "postgres://hms:hms_dev_pwd@localhost:5432/hms?sslmode=disable" force {{version}}

# 迁移往返测试 (CI 同款, 含跨分区插入用例)
migrate-roundtrip:
    powershell -NoProfile -File "{{root}}/scripts/test-migrate-roundtrip.ps1"

# fail-closed 权限映射完整性检查 (CI 门禁同款)
check-perm-map:
    python "{{root}}/scripts/check_perm_mapping.py"

# 后端构建与测试
build-backend:
    cmake -S "{{root}}/hms-backend" -B "{{root}}/hms-backend/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
    cmake --build "{{root}}/hms-backend/build" --config Release -j

test-backend:
    ctest --test-dir "{{root}}/hms-backend/build" -C Release --output-on-failure

# 前端
dev-web:
    powershell -NoProfile -Command "cd '{{root}}/hms-web'; npm install; npm run dev"

dev-dashboard:
    powershell -NoProfile -Command "cd '{{root}}/hms-dashboard'; npm install; npm run dev"

# E2E
e2e-m1:
    powershell -NoProfile -File "{{root}}/tests/e2e/m1_flow.ps1"

# 报工并发超报防护测试 (需先 dev-up + 启动后端)
e2e-concurrent-report:
    powershell -NoProfile -File "{{root}}/tests/e2e/concurrent_report.ps1"

# IoT 模拟器 (无需硬件, 直发 MQ)
iot-sim:
    python "{{root}}/scripts/iot_simulator.py"

# M1 性能基线 (需安装 k6 CLI): 500 VU 混合场景 10 分钟
perf-m1:
    k6 run "{{root}}/perf/k6/m1_baseline.js"
