# tests/e2e/m2_device_heartbeat.ps1 — P1-2.9A 设备心跳写入 + 离线判定 E2E
# 场景: ① 设备上报 -> iot_devices.last_heartbeat_at 刷新且 status=1 (恢复在线)
#       ② 心跳超时 (psql 快进到 70s 前, 模拟停报) -> DeviceMonitor 置离线 + OFFLINE 告警落库
#       ③ 重新上报 -> status 回 1 (恢复在线, 满足验收②)
# 前置: just dev-up 已启动中间件, mes-backend 运行于 :8088, DeviceMonitor 随进程启动 (10s 扫描周期)。
$ErrorActionPreference = "Stop"
[System.Net.ServicePointManager]::Expect100Continue = $false
$base = if ($env:MES_API_BASE) { $env:MES_API_BASE } else { "http://127.0.0.1:8088" }
$suffix = Get-Date -Format "HHmmss"
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
        if ($txt) {
            try { return ($txt | ConvertFrom-Json) } catch {}
        }
        if ($httpStatus -gt 0) { return @{ code = $httpStatus } }
        throw
    }
}

function Assert([bool]$cond, [string]$name) {
    if ($cond) { $script:pass++; Write-Host "  [PASS] $name" }
    else { $script:fail++; Write-Host "  [FAIL] $name" }
}

function Psql([string]$sql) {
    return ((docker exec mes-postgres psql -U mes -d mes -t -A -c $sql) | Out-String).Trim()
}

Write-Host "==> admin 登录" -ForegroundColor Cyan
$login = Api "POST" "/api/v1/auth/login" @{ username = "admin"; password = "password" }
if ($login.code -ne 200) { throw "admin 登录失败" }
$tk = $login.data.access_token

# ---- 0. 确保测试设备存在 (id=1, 模拟器 device_id 从 1 起) ----
Write-Host "`n== 准备: 测试设备 id=1 ==" -ForegroundColor Cyan
$exists = Psql "SELECT 1 FROM iot_devices WHERE id = 1 AND deleted = FALSE"
if ($exists -ne "1") {
    $dev = Api "POST" "/api/v1/iot/devices" @{
        device_code = "HB-SIM-0001"; device_name = "心跳测试设备 $suffix"; protocol = "Modbus"
        status = 0
    } $tk
    Assert ($dev.code -eq 200) "创建设备 HB-SIM-0001, code=$($dev.code)"
}
# 重置为在线但心跳为 70s 前 (模拟已停报)
Psql "UPDATE iot_devices SET status = 1, last_heartbeat_at = NOW() - INTERVAL '70 seconds' WHERE id = 1" | Out-Null
$baselineAlerts = [int64](Psql "SELECT count(*) FROM iot_alerts WHERE device_id = 1")

# ---- ① 上报 -> 心跳刷新 + 在线 ----
Write-Host "`n== 场景①: 上报刷新 last_heartbeat_at (status=1) ==" -ForegroundColor Cyan
$simOut = python "$PSScriptRoot/../../scripts/iot_simulator.py" --count 3 --devices 1 2>&1 | Out-String
Write-Host ($simOut.Trim())
Assert ($simOut -like "*done: 3*") "模拟器发布 3 条 (device 1)"
$hb = $null
for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Seconds 2
    $hb = Psql "SELECT EXTRACT(EPOCH FROM (NOW() - last_heartbeat_at)) < 10 FROM iot_devices WHERE id = 1"
    if ($hb -eq "t") { break }
}
Assert ($hb -eq "t") "上报后 last_heartbeat_at 刷新 (<10s 前)"
$st1 = Psql "SELECT status FROM iot_devices WHERE id = 1"
Assert ($st1 -eq "1") "上报后 status=1 (在线)"

# ---- ② 心跳超时 -> 离线 + OFFLINE 告警 ----
Write-Host "`n== 场景②: 心跳超时 60s -> 离线 + OFFLINE 告警 ==" -ForegroundColor Cyan
Psql "UPDATE iot_devices SET last_heartbeat_at = NOW() - INTERVAL '70 seconds' WHERE id = 1" | Out-Null
$offline = $false
for ($i = 0; $i -lt 18; $i++) {
    Start-Sleep -Seconds 3  # DeviceMonitor 10s 周期, 等 1-2 轮
    $st = Psql "SELECT status FROM iot_devices WHERE id = 1"
    if ($st -eq "0") { $offline = $true; break }
}
Assert ($offline) "超时后 status 自动置 0 (离线), 实际=$st"
$alertCnt = [int64](Psql "SELECT count(*) FROM iot_alerts WHERE device_id = 1 AND alert_type = 'OFFLINE'")
Assert ($alertCnt -gt $baselineAlerts) "生成 OFFLINE 告警落库 (count=$alertCnt)"

# ---- ③ 恢复上报 -> 重新在线 ----
Write-Host "`n== 场景③: 恢复上报 -> 重新在线 ==" -ForegroundColor Cyan
$simOut2 = python "$PSScriptRoot/../../scripts/iot_simulator.py" --count 3 --devices 1 2>&1 | Out-String
Write-Host ($simOut2.Trim())
$st2 = $null
for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Seconds 2
    $st2 = Psql "SELECT status FROM iot_devices WHERE id = 1"
    if ($st2 -eq "1") { break }
}
Assert ($st2 -eq "1") "恢复上报后 status 回 1 (重新在线)"

Write-Host "`n结果: PASS=$pass FAIL=$fail"
if ($fail -gt 0) { exit 1 }
