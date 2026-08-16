# tests/e2e/m2_dashboard_fallback.ps1 — P2-3.5 大屏降级链路稳定性 E2E
# 覆盖:
#   场景 A (默认) 降级链路: 杀 WS 后端实例 -> WS 连续重连失败 (等价前端 retry>=3 切降级态)
#                         -> 降级轮询源不可达 -> 恢复实例 -> 30s 内 WS 回连成功 + 推送恢复 (前端回切)
#   场景 B (-LoadTest)    1000 WS 连接压测回归 (scripts/ws_load.py --mode load)
# 前置: just dev-up 已启动中间件与迁移, hms-backend 运行于 :8088 (默认)。
#       -BackendCmd 指定后端可执行文件时, 场景 A 自动完成 杀->重启 闭环;
#       不指定且后端被外部编排恢复时, 脚本在等待窗口内探测恢复即可。
# 用法:
#   powershell -File tests/e2e/m2_dashboard_fallback.ps1
#   powershell -File tests/e2e/m2_dashboard_fallback.ps1 -BackendCmd "E:\...\hms-backend.exe" -LoadTest
param(
    [string]$BackendCmd = "",
    [switch]$LoadTest
)
$ErrorActionPreference = "Stop"
[System.Net.ServicePointManager]::Expect100Continue = $false
$base = if ($env:HMS_API_BASE) { $env:HMS_API_BASE } else { "http://127.0.0.1:8088" }
$pass = 0; $fail = 0
$degradedRetry = 3        # 前端 useChannel: retry>=3 置降级态 (连续重连失败)
$recoverTimeoutSec = 30   # 恢复窗口: 后端重启后 WS 回连成功必须 < 30s

function Check([string]$name, [bool]$cond) {
    if ($cond) { $script:pass++; Write-Host "[PASS] $name" }
    else { $script:fail++; Write-Host "[FAIL] $name" }
}

# ---- WS 辅助 (与 m2_ws_smoke.ps1 同款) ----
function TryRecv($ws, [int]$sec) {
    $buf = New-Object byte[] 65536
    $seg = New-Object System.ArraySegment[byte] -ArgumentList @(,$buf)
    $sb = New-Object Text.StringBuilder
    while ($true) {
        $task = $ws.ReceiveAsync($seg, [System.Threading.CancellationToken]::None)
        $done = [System.Threading.Tasks.Task]::WhenAny(@($task, [System.Threading.Tasks.Task]::Delay($sec * 1000))).Result
        if ($done -ne $task) { return $null }
        if ($task.IsFaulted) { throw $task.Exception.InnerException }
        $res = $task.Result
        [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $res.Count))
        if ($res.EndOfMessage) { break }
    }
    return $sb.ToString()
}

function SendMsg($ws, [string]$text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($text)
    $seg = New-Object System.ArraySegment[byte] -ArgumentList @(,$bytes)
    $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true,
        [System.Threading.CancellationToken]::None).Wait()
}

# 尝试连接: 成功返回 ws 对象, 失败返回 $null (不抛异常, 用于降级探测)
function TryConnectWs([string]$url) {
    try {
        $ws = New-Object System.Net.WebSockets.ClientWebSocket
        $cts = New-Object System.Threading.CancellationTokenSource
        $cts.CancelAfter(6000)
        $ws.ConnectAsync([Uri]$url, $cts.Token).Wait()
        return $ws
    } catch {
        if ($ws) { try { $ws.Dispose() } catch {} }
        return $null
    }
}

# ---- 登录 ----
Write-Host "==> admin 登录" -ForegroundColor Cyan
$loginBody = [Text.Encoding]::UTF8.GetBytes((@{ username = "admin"; password = "password" } | ConvertTo-Json))
$login = Invoke-RestMethod -Uri "$base/api/v1/auth/login" -Method Post -ContentType "application/json" -Body $loginBody
$token = $login.data.access_token
Check "admin 登录取 token" (-not [string]::IsNullOrEmpty($token))
$wsUrl = "ws://127.0.0.1:8088/ws?token=$token"

# ================ 场景 A: 降级链路 ================
Write-Host "`n== 场景 A: 杀 WS 后端实例 -> 降级 -> 恢复回切 ==" -ForegroundColor Cyan

# A1. 基线: WS 连接 + 订阅 production.realtime 成功
Write-Host "--- A1 基线 WS 连接 ---"
$ws = TryConnectWs $wsUrl
Check "基线 WS 连接成功" ($null -ne $ws)
if ($null -ne $ws) {
    $welcome = TryRecv $ws 5
    SendMsg $ws '{"action":"subscribe","channel":"production.realtime"}'
    $ack = TryRecv $ws 5
    Check "welcome + 订阅 ack 收到" ($null -ne $welcome -and $null -ne $ack)
    $ws.Dispose()
}

# A2. 杀后端实例 (WS/REST 同实例, 等价 WS 链路不可达)
Write-Host "--- A2 杀后端实例 ---"
$proc = Get-Process -Name "hms-backend" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) {
    Stop-Process -Id $proc.Id -Force
    $proc.WaitForExit(10000) | Out-Null
    Check "后端进程已停止 (pid=$($proc.Id))" (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue))
} else {
    Write-Host "  [SKIP] 未发现 hms-backend 进程, 假定外部编排已停止" -ForegroundColor Yellow
}

# A3. 前端降级判定: 连续 $degradedRetry 次重连失败 (retry>=3 -> degraded=true)
Write-Host "--- A3 连续 $degradedRetry 次重连失败 (等价前端切降级态) ---"
$failCnt = 0
for ($i = 0; $i -lt $degradedRetry; $i++) {
    if ($null -eq (TryConnectWs $wsUrl)) { $failCnt++ }
    Start-Sleep -Milliseconds 300
}
Check "连续 $degradedRetry 次 WS 重连失败 (retry>=$degradedRetry 置降级态)" ($failCnt -eq $degradedRetry)

# A4. 降级轮询源: REST 历史数据接口应不可达 (降级态仅展示本地缓存/重试)
Write-Host "--- A4 降级轮询源不可达 ---"
$pollDown = $true
try {
    Invoke-RestMethod -Uri "$base/api/v1/production/work-orders?page=1&page_size=1&status=3" -Method Get `
        -Headers @{ Authorization = "Bearer $token" } -TimeoutSec 3 | Out-Null
    $pollDown = $false
} catch { $pollDown = $true }
Check "降级期 REST 轮询源不可达 (链路整体中断)" $pollDown

# A5. 恢复实例 (可选 -BackendCmd; 否则等待外部恢复)
Write-Host "--- A5 恢复后端实例 ---"
if ($BackendCmd) {
    if (-not (Test-Path $BackendCmd)) { throw "BackendCmd 不存在: $BackendCmd" }
    Start-Process -FilePath $BackendCmd -WindowStyle Hidden
    Write-Host "  已启动: $BackendCmd"
} else {
    Write-Host "  未提供 -BackendCmd, 等待外部编排恢复后端..." -ForegroundColor Yellow
}
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$recovered = $false
while ($sw.Elapsed.TotalSeconds -lt $recoverTimeoutSec -and -not $recovered) {
    Start-Sleep -Seconds 1
    try {
        $h = Invoke-RestMethod -Uri "$base/healthz" -Method Get -TimeoutSec 3
        if ($h.code -eq 200 -or $null -ne $h) { $recovered = $true }
    } catch { $recovered = $false }
}
Check "后端 $($recoverTimeoutSec)s 内恢复 (healthz 可达)" $recovered

# A6. 回切: WS 重连成功 + 收到推送 (前端 onopen -> degraded=false)
Write-Host "--- A6 WS 回连 + 推送恢复 (前端回切) ---"
$ws = $null
$reconnectSec = 0.0
$sw.Restart()
while ($sw.Elapsed.TotalSeconds -lt $recoverTimeoutSec -and $null -eq $ws) {
    $ws = TryConnectWs $wsUrl
    if ($null -eq $ws) { Start-Sleep -Milliseconds 800 }
}
$reconnectSec = [Math]::Round($sw.Elapsed.TotalSeconds, 1)
Check "WS 回连成功 (耗时 ${reconnectSec}s < ${recoverTimeoutSec}s)" ($null -ne $ws -and $reconnectSec -lt $recoverTimeoutSec)
if ($null -ne $ws) {
    $welcome2 = TryRecv $ws 5
    SendMsg $ws '{"action":"subscribe","channel":"alert"}'
    $ack2 = TryRecv $ws 5
    Check "恢复后 welcome + 订阅 ack 收到 (实时链路恢复, 前端回切)" ($null -ne $welcome2 -and $null -ne $ack2)
    $ws.Dispose()
}

# ================ 场景 B: 1000 WS 连接压测回归 ================
if ($LoadTest) {
    Write-Host "`n== 场景 B: 1000 WS 连接压测回归 ==" -ForegroundColor Cyan
    $py = if ($env:HMS_PY) { $env:HMS_PY } else { "python" }
    $out = & $py scripts/ws_load.py --token $token --mode load --connections 1000 --duration 60 2>&1 | Out-String
    Write-Host $out
    try {
        $res = $out | ConvertFrom-Json
        Check "1000 WS 连接压测 gate_1000_connections" ($res.gate_1000_connections -eq $true)
        Write-Host "  connected_30s=$($res.connected_30s) survived=$($res.survived_full_duration)"
    } catch {
        Check "1000 WS 连接压测 gate_1000_connections (输出解析失败)" $false
    }
} else {
    Write-Host "`n[SKIP] 场景 B (1000 连接回归) 需 -LoadTest 开关" -ForegroundColor Yellow
}

Write-Host "`n结果: PASS=$pass FAIL=$fail"
if ($fail -gt 0) { exit 1 }
