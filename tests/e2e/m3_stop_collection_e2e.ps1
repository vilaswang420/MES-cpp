# tests/e2e/m3_stop_collection_e2e.ps1 — M3 停采链路端到端验证 (P4-5.2)
# 完整链路: 建单 -> 开工 -> 报满 -> outbox(cmd.stop_collection)
#   -> StopCollectionHandler 二次投递(cmd.stop.{device_id})
#   -> hms-iot CmdConsumer 暂停 DevicePoller -> 设备停止上报
#   -> 重启 hms-iot -> 恢复采集 (暂停态仅内存, 不持久化)
#   -> 幂等: 同线第二张工单再次报满, 重复停采指令不报错
#   -> 拓扑隔离: cmd.stop.# 仅进 iot.cmd.collector.queue, 不与后端 iot.cmd.queue 竞争消费
#
# 前置:
#   1. just dev-up (hms-postgres / hms-redis / hms-rabbitmq 三容器就绪, 迁移已应用)
#   2. hms-backend 运行于 :8088 (HMS_API_BASE 可覆盖)
#   3. hms-iot 运行且 backend_pwd 已配置 (默认容器名 hms-iot, HMS_IOT_RESTART_CMD 可覆盖重启命令)
#   4. pymodbus 已安装 (脚本自动拉起 scripts/modbus_slave_sim.py, 端口 15020)
# 执行: powershell -File tests/e2e/m3_stop_collection_e2e.ps1
$ErrorActionPreference = "Stop"
# .NET HttpWebRequest 默认对 POST 发 Expect: 100-continue, 与服务端竞态会导致偶发空体 400
[System.Net.ServicePointManager]::Expect100Continue = $false
$base = if ($env:HMS_API_BASE) { $env:HMS_API_BASE } else { "http://127.0.0.1:8088" }
$simPort = if ($env:HMS_SIM_PORT) { [int]$env:HMS_SIM_PORT } else { 15020 }
$iotRestart = if ($env:HMS_IOT_RESTART_CMD) { $env:HMS_IOT_RESTART_CMD } else { "docker restart hms-iot" }
$py = if ($env:HMS_PYTHON) { $env:HMS_PYTHON } else { "python" }
$pass = 0; $fail = 0

function Api([string]$method, [string]$path, $body = $null, [string]$token = "") {
    $headers = @{ "Content-Type" = "application/json" }
    if ($token) { $headers["Authorization"] = "Bearer $token" }
    $json = if ($null -ne $body) { $b = $body | ConvertTo-Json -Depth 8 -Compress; if ([string]::IsNullOrWhiteSpace($b)) { "{}" } else { $b } } else { $null }
    try {
        if ($null -ne $json) {
            $resp = Invoke-RestMethod -Uri "$base$path" -Method $method -Headers $headers -Body ([System.Text.Encoding]::UTF8.GetBytes($json))
        } else {
            $resp = Invoke-RestMethod -Uri "$base$path" -Method $method -Headers $headers
        }
        return $resp
    } catch {
        $txt = "$($_.ErrorDetails.Message)"
        $r = $_.Exception.Response
        $httpStatus = if ($r) { [int]$r.StatusCode } else { 0 }
        if ($txt) { try { return ($txt | ConvertFrom-Json) } catch {} }
        if ($httpStatus -gt 0) { return @{ code = $httpStatus } }
        throw
    }
}

function Assert([bool]$cond, [string]$name) {
    if ($cond) { $script:pass++; Write-Host "  [PASS] $name" }
    else { $script:fail++; Write-Host "  [FAIL] $name" -ForegroundColor Red }
}

function Step([string]$name) { Write-Host "`n==> $name" -ForegroundColor Cyan }

# iot_raw_data 中该设备 10 分钟内入库条数 (证明"正在上报")
function Raw-Count([int64]$deviceId) {
    $c = docker exec hms-postgres psql -U hms -d hms -t -A -c `
        "SELECT COUNT(*) FROM iot_raw_data WHERE device_id = $deviceId AND collected_at > NOW() - INTERVAL '10 minutes';"
    return [int64]($c -join "")
}

# 连续两次采样判断条数是否仍在增长 (间隔 $sec 秒)
function Wait-Rising([int64]$deviceId, [int]$timeoutSec, [string]$what) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    $prev = Raw-Count $deviceId
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        $cur = Raw-Count $deviceId
        if ($cur -gt $prev) { return $true }
        $prev = $cur
    }
    return $false
}

# 连续两次采样判断条数已冻结 (停采生效, 允许 2s 传播 + 采样窗口)
function Wait-Frozen([int64]$deviceId, [int]$timeoutSec) {
    Start-Sleep -Seconds 3   # 给 cmd.stop.{id} 消费 + poller 暂停留传播时间 (验收: 2s 内)
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    $prev = Raw-Count $deviceId
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 5
        $cur = Raw-Count $deviceId
        if ($cur -eq $prev) { return $true }
        $prev = $cur
    }
    return $false
}

$suffix = Get-Date -Format "HHmmss"

# ---- 0. 前置检查 ----
Step "前置检查"
$hz = Invoke-RestMethod -Uri "$base/healthz"
Assert ($hz.code -eq 200) "hms-backend healthz"
$pgOk = docker exec hms-postgres psql -U hms -d hms -t -A -c "SELECT 1;"
Assert ("$pgOk" -trim -eq "1") "hms-postgres 容器可用"
$mqOk = docker exec hms-rabbitmq rabbitmq-diagnostics -q ping
Assert ($LASTEXITCODE -eq 0) "hms-rabbitmq 容器可用"

# 拓扑隔离前置: 两条 cmd 队列都必须存在 (iot.cmd.queue=后端消费原始指令, collector=cmd.stop.#)
$queues = docker exec hms-rabbitmq rabbitmqctl list_queues -q name
Assert ($queues -match "iot\.cmd\.queue") "iot.cmd.queue 存在 (后端 StopCollectionHandler)"
Assert ($queues -match "iot\.cmd\.collector\.queue") "iot.cmd.collector.queue 存在 (hms-iot CmdConsumer)"

# 拉起 Modbus 从站模拟器 (端口 15020, 免 root; 已在跑则复用)
$simProc = $null
$simUp = Test-NetConnection -ComputerName 127.0.0.1 -Port $simPort -InformationLevel Quiet -WarningAction SilentlyContinue
if (-not $simUp) {
    $simProc = Start-Process -FilePath $py -ArgumentList @(
        "$PSScriptRoot\..\..\scripts\modbus_slave_sim.py", "--host", "127.0.0.1", "--port", "$simPort"
    ) -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 2
}
$simUp2 = Test-NetConnection -ComputerName 127.0.0.1 -Port $simPort -InformationLevel Quiet -WarningAction SilentlyContinue
Assert $simUp2 "Modbus 从站模拟器就绪 (127.0.0.1:$simPort)"

# ---- 1. admin 登录 + 建主数据 ----
Step "登录 + 创建产品/产线/工艺/设备/传感器"
$login = Api "POST" "/api/v1/auth/login" @{ username = "admin"; password = "password" }
Assert ($login.code -eq 200) "admin 登录"
$tk = $login.data.access_token

$product = Api "POST" "/api/v1/production/products" @{ product_code = "E2E-P-$suffix"; product_name = "停采E2E产品 $suffix"; unit = "PCS" } $tk
Assert ($product.code -eq 200) "创建产品"
$line = Api "POST" "/api/v1/production/lines" @{ line_code = "E2E-L-$suffix"; line_name = "停采E2E产线 $suffix" } $tk
Assert ($line.code -eq 200) "创建产线"
$lineId = $line.data.id
$process = Api "POST" "/api/v1/production/processes" @{ process_code = "E2E-R-$suffix"; process_name = "停采E2E工艺 $suffix"; product_id = $product.data.id
    steps = @( @{ step_seq = 1; step_name = "组装"; step_code = "E2E-S1-$suffix" } ) } $tk
Assert ($process.code -eq 200) "创建工艺(1 工序)"

# 设备: modbus_tcp 指向模拟器, 绑定产线 (StopCollectionHandler 按 line_id 关联)
$dev = Api "POST" "/api/v1/iot/devices" @{
    device_code = "E2E-DEV-$suffix"; device_name = "停采E2E设备"; protocol = "modbus_tcp"
    line_id = $lineId; ip_address = "127.0.0.1"; port = $simPort; status = 1
    connection_config = @{ unit_id = 1; poll_interval_ms = 1000 }
} $tk
Assert ($dev.code -eq 200 -and $dev.data.id -gt 0) "创建设备 (绑定产线 line_id=$lineId)"
$devId = $dev.data.id

# 传感器: 寄存器映射与 modbus_slave_sim.py 一致 (40001 温度 scale 0.1 / 40003 转速)
$s1 = Api "POST" "/api/v1/iot/devices/$devId/sensors" @{ sensor_code = "E2E-TEMP-$suffix"; sensor_name = "温度"; data_type = "INT16"; unit = "C"; register_addr = "40001"; scale_factor = 0.1; sample_interval = 1000 } $tk
Assert ($s1.code -eq 200) "创建温度传感器 (40001, scale=0.1)"
$s2 = Api "POST" "/api/v1/iot/devices/$devId/sensors" @{ sensor_code = "E2E-RPM-$suffix"; sensor_name = "转速"; data_type = "UINT16"; unit = "rpm"; register_addr = "40003"; scale_factor = 1.0; sample_interval = 1000 } $tk
Assert ($s2.code -eq 200) "创建转速传感器 (40003, scale=1.0)"

# ---- 2. 重启 hms-iot 加载新设备配置 (ConfigLoader 仅启动时拉取) ----
Step "重启 hms-iot 加载新设备"
Invoke-Expression $iotRestart | Out-Null
Assert ($LASTEXITCODE -eq 0) "重启 hms-iot ($iotRestart)"
$flowing = Wait-Rising $devId 45 "重启后数据流入"
Assert $flowing "设备开始上报 (iot_raw_data 持续增长)"

# ---- 3. 建单 -> 排产 -> 下达 -> 开工 -> 报满 ----
Step "建单并流转: 报满自动完工触发停采"
$wo = Api "POST" "/api/v1/production/work-orders" @{ product_id = $product.data.id; process_id = $process.data.id; line_id = $lineId; plan_qty = 5; priority = 5 } $tk
Assert ($wo.code -eq 200) "创建工单"
$woId = $wo.data.id
$null = Api "PUT" "/api/v1/production/work-orders/$woId/schedule" @{} $tk
$null = Api "PUT" "/api/v1/production/work-orders/$woId/release" @{} $tk
$null = Api "PUT" "/api/v1/production/work-orders/$woId/start" @{} $tk
$det = Api "GET" "/api/v1/production/work-orders/$woId" $null $tk
Assert ($det.code -eq 200 -and $det.data.status -eq 3) "工单开工 (status=3)"

$r = Api "POST" "/api/v1/production/work-orders/$woId/report" @{ step_seq = 1; good_qty = 5 } $tk
Assert ($r.code -eq 200 -and $r.data.finished -eq $true) "报满自动完工 (finished=true)"

# ---- 4. 二次投递 outbox 落库并投出 ----
Step "验证停采指令二次投递 (cmd.stop.{device_id})"
$relayOk = $false
for ($i = 0; $i -lt 15; $i++) {
    $n = docker exec hms-postgres psql -U hms -d hms -t -A -c `
        "SELECT COUNT(*) FROM mq_outbox WHERE routing_key = 'cmd.stop.$devId' AND status = 1 AND payload LIKE '%""work_order_id"": $woId%';"
    if ([int]($n -join "") -ge 1) { $relayOk = $true; break }
    Start-Sleep -Seconds 1
}
Assert $relayOk "outbox 已投递 cmd.stop.$devId (wo=$woId)"

# ---- 5. 验收: 完工后设备停止上报 ----
Step "验收: 设备停止上报"
$frozen = Wait-Frozen $devId 30
Assert $frozen "报满后设备停止上报 (iot_raw_data 冻结)"

# ---- 6. 幂等: 同线第二张工单再次报满, 重复停采不报错 ----
Step "幂等: 重复停采指令"
$wo2 = Api "POST" "/api/v1/production/work-orders" @{ product_id = $product.data.id; process_id = $process.data.id; line_id = $lineId; plan_qty = 3; priority = 4 } $tk
Assert ($wo2.code -eq 200) "同线第二张工单"
$wo2Id = $wo2.data.id
$null = Api "PUT" "/api/v1/production/work-orders/$wo2Id/schedule" @{} $tk
$null = Api "PUT" "/api/v1/production/work-orders/$wo2Id/release" @{} $tk
$null = Api "PUT" "/api/v1/production/work-orders/$wo2Id/start" @{} $tk
$r2 = Api "POST" "/api/v1/production/work-orders/$wo2Id/report" @{ step_seq = 1; good_qty = 3 } $tk
Assert ($r2.code -eq 200 -and $r2.data.finished -eq $true) "第二张工单报满 (重复停采链路无报错)"
Start-Sleep -Seconds 5
$frozen2 = Wait-Frozen $devId 20
Assert $frozen2 "重复停采后仍冻结 (幂等, poller 不崩溃)"

# ---- 7. 拓扑隔离: 队列不竞争消费 ----
Step "拓扑隔离: cmd 队列消费方检查"
# iot.cmd.queue (后端) 与 iot.cmd.collector.queue (hms-iot) 消费者 tag/连接不同;
# cmd.stop.# 只进 collector 队列, 后端队列不应堆积 cmd.stop 消息
$consumers = docker exec hms-rabbitmq rabbitmqctl list_consumers -q queue_name ack_settings
$cmdQ = ($consumers | Select-String "iot\.cmd\.queue\s").Line
$colQ = ($consumers | Select-String "iot\.cmd\.collector\.queue").Line
Assert (-not [string]::IsNullOrWhiteSpace($cmdQ)) "iot.cmd.queue 有后端消费者"
Assert (-not [string]::IsNullOrWhiteSpace($colQ)) "iot.cmd.collector.queue 有 hms-iot 消费者"
$qDepths = docker exec hms-rabbitmq rabbitmqctl list_queues -q name messages_ready
$cmdReady = ($qDepths | Select-String "^iot\.cmd\.queue\s+(\d+)").Matches.Groups[1].Value
Assert ([int]$cmdReady -le 1) "iot.cmd.queue 无堆积 (cmd.stop_collection 已被后端消费, ready=$cmdReady)"

# ---- 8. 重启恢复: 暂停态仅内存, 重启 hms-iot 恢复采集 ----
Step "重启恢复: 设备恢复采集"
Invoke-Expression $iotRestart | Out-Null
Assert ($LASTEXITCODE -eq 0) "再次重启 hms-iot"
$resumed = Wait-Rising $devId 60 "重启后恢复流入"
Assert $resumed "重启后设备恢复采集 (暂停状态未持久化)"

# ---- 清理 ----
if ($simProc -and -not $simProc.HasExited) { Stop-Process -Id $simProc.Id -Force -ErrorAction SilentlyContinue }

Write-Host "`n结果: PASS=$pass FAIL=$fail"
if ($fail -gt 0) { exit 1 }
