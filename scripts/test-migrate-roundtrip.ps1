# 迁移往返测试 (计划任务 5/8, 本地版与 CI migrations job 等价):
# up 全量 -> 跨分区插入用例 -> down 全量 -> 再 up 全量。
# 前置: docker compose dev 环境已起 (just infra-up), migrate CLI 与 psql/docker exec 可用。
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$migrations = ((Join-Path $root "hms-backend\migrations") -replace "\\", "/")
# 独立测试库, 避免污染开发库 hms
$dsn = "postgres://hms:hms_dev_pwd@localhost:5432/hms_roundtrip?sslmode=disable"

function Invoke-Sql([string]$Sql) {
    docker exec -i hms-postgres psql -U hms -d hms_roundtrip -c $Sql
    if ($LASTEXITCODE -ne 0) { throw "SQL 执行失败: $Sql" }
}

Write-Host "==> 准备测试库 hms_roundtrip" -ForegroundColor Cyan
docker exec -i hms-postgres psql -U hms -d postgres -c "DROP DATABASE IF EXISTS hms_roundtrip;" | Out-Null
docker exec -i hms-postgres psql -U hms -d postgres -c "CREATE DATABASE hms_roundtrip;" | Out-Null

Write-Host "==> migrate up (全量)" -ForegroundColor Cyan
migrate -path $migrations -database $dsn up
if ($LASTEXITCODE -ne 0) { throw "migrate up 失败" }

Write-Host "==> 跨分区插入用例 (审计表当月/次月分区)" -ForegroundColor Cyan
Invoke-Sql "INSERT INTO sys_audit_logs (user_id, username, module, operation, method, request_url, response_code, created_at) VALUES (1,'roundtrip','system','当月分区写入','GET','/roundtrip',200, NOW());"
Invoke-Sql "INSERT INTO sys_audit_logs (user_id, username, module, operation, method, request_url, response_code, created_at) VALUES (1,'roundtrip','system','次月分区写入','GET','/roundtrip',200, NOW() + INTERVAL '1 month');"

Write-Host "==> migrate down (全量回退)" -ForegroundColor Cyan
@("y") | migrate -path $migrations -database $dsn down -all
if ($LASTEXITCODE -ne 0) { throw "migrate down 失败" }

Write-Host "==> migrate up (往返后重建)" -ForegroundColor Cyan
migrate -path $migrations -database $dsn up
if ($LASTEXITCODE -ne 0) { throw "二次 migrate up 失败" }

Write-Host "==> 清理测试库" -ForegroundColor Cyan
docker exec -i hms-postgres psql -U hms -d postgres -c "DROP DATABASE hms_roundtrip;" | Out-Null

Write-Host "迁移往返测试通过" -ForegroundColor Green
