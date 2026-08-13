# M2 WebSocket 广播链路冒烟 (计划任务 21 / 设计文档 4.9 节)
# 覆盖: /ws 与 /ws/dashboard query token 鉴权、订阅契约频道、Redis PUBLISH -> WS 信封推送、
#       非法频道拒绝、无 token / 无效 token fail-closed 断开
# 注1: .NET ClientWebSocket 的 ReceiveAsync 不响应 CancellationToken, 超时用 Task.WhenAny 实现
# 注2: docker exec argv 会吃引号, PUBLISH 合法 JSON 走 docker cp + sh 脚本文件
$ErrorActionPreference = "Stop"
$base = "http://127.0.0.1:8088"
$pass = 0; $fail = 0

function Check([string]$name, [bool]$cond) {
    if ($cond) { $script:pass++; Write-Host "[PASS] $name" }
    else { $script:fail++; Write-Host "[FAIL] $name" }
}

function TryRecv($ws, [int]$sec) {
    $buf = New-Object byte[] 65536
    $seg = New-Object System.ArraySegment[byte] -ArgumentList @(,$buf)
    $sb = New-Object Text.StringBuilder
    while ($true) {
        $task = $ws.ReceiveAsync($seg, [System.Threading.CancellationToken]::None)
        $done = [System.Threading.Tasks.Task]::WhenAny(@($task, [System.Threading.Tasks.Task]::Delay($sec * 1000))).Result
        if ($done -ne $task) { return $null }  # 超时
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

function ConnectWs([string]$url) {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    $cts = New-Object System.Threading.CancellationTokenSource
    $cts.CancelAfter(8000)
    $ws.ConnectAsync([Uri]$url, $cts.Token).Wait()
    return $ws
}

function RedisPublish([string]$channel, [string]$payloadJson) {
    $tmp = Join-Path $env:TEMP "ws_pub_payload.json"
    [IO.File]::WriteAllText($tmp, $payloadJson)
    docker cp $tmp hms-redis:/tmp/ws_pub_payload.json | Out-Null
    $sh = Join-Path $env:TEMP "ws_pub.sh"
    [IO.File]::WriteAllText($sh, "#!/bin/sh`nredis-cli PUBLISH ws:broadcast:$channel `"`$(cat /tmp/ws_pub_payload.json)`"`n")
    docker cp $sh hms-redis:/tmp/ws_pub.sh | Out-Null
    return (docker exec hms-redis sh /tmp/ws_pub.sh)
}

# ---- admin 登录取 token ----
$loginBody = [Text.Encoding]::UTF8.GetBytes((@{ username = "admin"; password = "password" } | ConvertTo-Json))
$login = Invoke-RestMethod -Uri "$base/api/v1/auth/login" -Method Post -ContentType "application/json" -Body $loginBody
$token = $login.data.access_token
Check "admin 登录取 token" (-not [string]::IsNullOrEmpty($token))

# ---- /ws 升级 + welcome ----
$ws = ConnectWs "ws://127.0.0.1:8088/ws?token=$token"
Check "/ws?token 升级成功" ($ws.State -eq "Open")
$welcome = TryRecv $ws 5
Check "收到 welcome 消息" ($welcome -like '*welcome*')

# ---- 订阅 alert 频道 ----
SendMsg $ws (@{ action = "subscribe"; channel = "alert" } | ConvertTo-Json -Compress)
$sub = TryRecv $ws 5
Check "订阅 alert 频道成功" (($sub -like '*subscribed*') -and ($sub -like '*alert*'))

# ---- Redis PUBLISH -> 信封推送 ----
$pubN = RedisPublish "alert" '{"device_code":"WS-SMOKE","level":"warning","message":"ws smoke test"}'
Check "Redis PUBLISH 有订阅者接收" ($pubN -ge 1)
$env1 = TryRecv $ws 5
Check "收到广播消息" (($null -ne $env1) -and ($env1 -like '*WS-SMOKE*'))
$envOk = $false
try {
    $envObj = $env1 | ConvertFrom-Json
    $envOk = ($envObj.version -eq "1.0") -and ($envObj.channel -eq "alert") -and ($envObj.ts) -and ($envObj.payload)
} catch {}
Check "信封符合契约 version/channel/ts/payload" $envOk

# ---- 非法频道拒绝 ----
SendMsg $ws (@{ action = "subscribe"; channel = "no.such.channel" } | ConvertTo-Json -Compress)
$err1 = TryRecv $ws 5
Check "非法频道返回 error" (($err1 -like '*error*') -and ($err1 -like '*no.such.channel*'))

# ---- /ws/dashboard 同样可用 ----
$ws2 = ConnectWs "ws://127.0.0.1:8088/ws/dashboard?token=$token"
Check "/ws/dashboard 升级成功" ($ws2.State -eq "Open")
$w2 = TryRecv $ws2 5
Check "/ws/dashboard welcome" ($w2 -like '*welcome*')

# ---- 无 token fail-closed ----
try {
    $ws3 = ConnectWs "ws://127.0.0.1:8088/ws"
    $msg3 = ""
    try { $msg3 = TryRecv $ws3 5 } catch {}
    Check "无 token 连接被拒绝" (($msg3 -like '*error*') -or ($ws3.State -ne "Open"))
    $ws3.Dispose()
} catch {
    Check "无 token 连接被拒绝" $true
}

# ---- 无效 token fail-closed ----
try {
    $ws4 = ConnectWs "ws://127.0.0.1:8088/ws?token=invalid.token.x"
    $msg4 = ""
    try { $msg4 = TryRecv $ws4 5 } catch {}
    Check "无效 token 被拒绝" (($msg4 -like '*error*') -or ($ws4.State -ne "Open"))
    $ws4.Dispose()
} catch {
    Check "无效 token 被拒绝" $true
}

$ws.Dispose(); $ws2.Dispose()
Write-Host "----- WS 冒烟: $pass 通过, $fail 失败 -----"
if ($fail -gt 0) { exit 1 }
