# m2_iot_smoke.ps1 — IoT 采集入库链路冒烟 (计划任务 18/19, M2 出口前置)
# 覆盖: 模拟器 1 万条消息全量入库 (iot_raw_data 分区表)、Redis device:latest 更新、
#       毒消息有界重试 (x-retry-count 3 次, retry.queue TTL 10s) 后进 iot.dlq
# 依赖: scripts/iot_simulator.py (pika)、后端 DataIngestHandler 消费中、topology 已声明
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path (Join-Path $root "scripts\iot_simulator.py"))) { $root = (Get-Location).Path }
$pass = 0; $fail = 0

function Check([string]$name, [bool]$cond) {
    if ($cond) { $script:pass++; Write-Host "[PASS] $name" }
    else { $script:fail++; Write-Host "[FAIL] $name" }
}

function Psql([string]$sql) {
    return ((docker exec mes-postgres psql -U mes -d mes -t -A -c $sql) | Out-String).Trim()
}

function QueueDepth([string]$queue) {
    $out = docker exec mes-rabbitmq rabbitmqctl list_queues name messages -q 2>$null
    foreach ($line in $out) {
        $parts = ($line | Out-String).Trim() -split "\s+"
        if ($parts[0] -eq $queue) { return [int]$parts[1] }
    }
    return -1
}

# ---- 基线 ----
$baseRaw = [int64](Psql "SELECT count(*) FROM iot_raw_data")
$baseDlq = QueueDepth "iot.dlq"
Write-Host "基线: iot_raw_data=$baseRaw iot.dlq=$baseDlq"

# ---- 1. 模拟器发布 1 万条 ----
$simOut = python (Join-Path $root "scripts\iot_simulator.py") --count 10000 --devices 100 2>&1 | Out-String
Write-Host ($simOut.Trim())
Check "模拟器 10000 条发布完成" ($simOut -like "*done: 10000*")

# ---- 2. 轮询入库进度 (批量 500 条/100ms, 10s 内应完成) ----
$ingested = $false
for ($i = 0; $i -lt 24; $i++) {
    Start-Sleep -Seconds 5
    $cnt = [int64](Psql "SELECT count(*) FROM iot_raw_data")
    if (($cnt - $baseRaw) -ge 10000) { $ingested = $true; Write-Host "  入库完成: +$($cnt - $baseRaw) (耗时约 $($i * 5 + 5)s)"; break }
}
Check "1 万条全量入库 iot_raw_data" $ingested

# ---- 3. Redis device:latest 更新 ----
$latest = docker exec mes-redis redis-cli GET "device:latest:1" 2>$null | Out-String
Check "Redis device:latest:1 已更新" ($latest -like '*DEV-SIM-0001*')

# ---- 4. 数据队列排空 ----
$dataDepth = QueueDepth "iot.data.queue"
Check "iot.data.queue 消费排空 (=0)" ($dataDepth -eq 0)

# ---- 5. 毒消息: 3 次有界重试后进 DLQ ----
$poisonOut = python (Join-Path $root "scripts\iot_simulator.py") --count 0 --poison 1 2>&1 | Out-String
Write-Host ($poisonOut.Trim())
Check "毒消息发布完成" ($poisonOut -like "*poison message #1*")

# 重试路径: data.queue -> retry.data -> iot.retry.queue (TTL 10s) -> data.retry -> data.queue, 共 3 轮约 30s+
$dlqOk = $false
for ($i = 0; $i -lt 18; $i++) {
    Start-Sleep -Seconds 5
    $dlq = QueueDepth "iot.dlq"
    if (($dlq - $baseDlq) -ge 1) { $dlqOk = $true; Write-Host "  毒消息进 DLQ (耗时约 $($i * 5 + 5)s)"; break }
}
Check "毒消息 3 次重试后进 iot.dlq" $dlqOk

Write-Host ""
Write-Host "=== m2-iot 冒烟结果: PASS=$pass FAIL=$fail ==="
if ($fail -gt 0) { exit 1 }
