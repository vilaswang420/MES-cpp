# M3 任务 29 发布演练编排: 蓝绿 (新版 10% 流量) + 回滚 + expand/contract + kill 实例 30s 自愈
# 前置: 双实例 A(8088)/B(8089) 运行中 (scripts/start_dual_instances.ps1)
$ErrorActionPreference = 'Stop'
$root = "e:\Work\Development\Projects\Hm_MES\New-MES"
$env:PATH = 'C:\Users\vilas\AppData\Local\Programs\DockerDesktop\resources\bin;' + $env:PATH

function SlotStats($n) {
    $green = 0; $blue = 0
    for ($i = 0; $i -lt $n; $i++) {
        $hdr = & curl.exe -sk -o NUL -D - https://127.0.0.1:8443/healthz 2>$null | Select-String 'X-MES-Slot'
        if ($hdr -match 'green') { $green++ } else { $blue++ }
    }
    return @{ green = $green; blue = $blue }
}

# ---- 阶段 1 expand: 启动新版实例 C (8090) ----
Write-Host "== 阶段1: expand 启动新版实例 C(8090) =="
Start-Process "$root\mes-backend\build\Release\mes-backend.exe" `
    -ArgumentList "config/drogon_config.c.json" -WorkingDirectory "$root\mes-backend" -WindowStyle Hidden
Start-Sleep 5
$c = (Invoke-RestMethod -Uri 'http://127.0.0.1:8090/healthz').data.status
Write-Host "instance C healthz = $c"

# ---- 阶段 2: 蓝绿 nginx (green 10%) ----
Write-Host "== 阶段2: 蓝绿 nginx 10% 灰度 =="
docker rm -f mes-nginx-drill 2>$null | Out-Null
docker run -d --name mes-nginx-drill -p 8443:8443 `
    -v "$($root -replace '\\','/')/deploy/nginx/nginx.drill.conf:/etc/nginx/nginx.conf:ro" `
    -v "$($root -replace '\\','/')/deploy/nginx/certs:/etc/nginx/certs:ro" `
    nginx:1.27-alpine | Out-Null
Start-Sleep 3
$r1 = SlotStats 200
$pct = [math]::Round($r1.green / 200.0 * 100, 1)
Write-Host "canary: green=$($r1.green) blue=$($r1.blue) green%=$pct"
$gate_canary = ($pct -ge 4) -and ($pct -le 18)
Write-Host "gate_canary_10pct = $gate_canary"

# ---- 阶段 3: 回滚 (0% 灰度) ----
Write-Host "== 阶段3: 回滚 green 0% =="
# 注: split_clients 不接受 0% (nginx: invalid percent value), 回滚标准做法是
# 删除 green 行由 * 全量承接; 挂载为 :ro, 宿主机侧改文件后容器内直接可见。
$drillConf = "$root\deploy\nginx\nginx.drill.conf"
$bakConf = "$root\deploy\nginx\nginx.drill.conf.bak"
Copy-Item $drillConf $bakConf -Force
# PS5.1 的 Get/Set-Content 默认编码会破坏 UTF-8 中文注释, 用 .NET API 无 BOM 读写
$utf8 = New-Object System.Text.UTF8Encoding($false)
$raw = [System.IO.File]::ReadAllText($drillConf, $utf8)
[System.IO.File]::WriteAllText($drillConf, ($raw -replace '(?m)^        10%     "green";\r?\n', ''), $utf8)
docker exec mes-nginx-drill grep -n 'green' /etc/nginx/nginx.conf
docker exec mes-nginx-drill nginx -s reload
Start-Sleep 2
$r2 = SlotStats 100
Write-Host "rollback: green=$($r2.green) blue=$($r2.blue)"
$gate_rollback = ($r2.green -eq 0)
Write-Host "gate_rollback_zero_traffic = $gate_rollback"

# ---- 阶段 4 contract: 撤下 green 与演练 nginx ----
Write-Host "== 阶段4: contract =="
Get-CimInstance Win32_Process -Filter "Name='mes-backend.exe'" |
    Where-Object { $_.CommandLine -like '*drogon_config.c.json*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force; Write-Host "contract: killed C pid=$($_.ProcessId)" }
docker rm -f mes-nginx-drill | Out-Null
if (Test-Path $bakConf) { Move-Item $bakConf $drillConf -Force }
$goneC = try { Invoke-RestMethod -Uri 'http://127.0.0.1:8090/healthz' -TimeoutSec 3; $true } catch { $false }
Write-Host "gate_contract_c_down = $(-not $goneC)"

# ---- 阶段 5: kill 实例 30s 自愈 ----
Write-Host "== 阶段5: kill 实例 B, 验证 A 接管与 30s 内恢复 =="
$leaderBefore = docker exec mes-redis redis-cli GET ws:realtime:leader
Get-CimInstance Win32_Process -Filter "Name='mes-backend.exe'" |
    Where-Object { $_.CommandLine -like '*drogon_config.b.json*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force; Write-Host "killed B pid=$($_.ProcessId)" }
$m1 = (Invoke-WebRequest -Uri 'http://127.0.0.1:8088/metrics' -UseBasicParsing).Content
$p1 = [regex]::Match($m1, 'mes_ws_broadcast_published_total (\d+)').Groups[1].Value
Start-Sleep 6   # 租约 PX 3000, 最迟 3s 接管
$leaderAfter = docker exec mes-redis redis-cli GET ws:realtime:leader
$m2 = (Invoke-WebRequest -Uri 'http://127.0.0.1:8088/metrics' -UseBasicParsing).Content
$p2 = [regex]::Match($m2, 'mes_ws_broadcast_published_total (\d+)').Groups[1].Value
$takeover = ($leaderAfter.Length -gt 0) -and ([int]$p2 -gt [int]$p1)
Write-Host "leader before=$leaderBefore after=$leaderAfter published $p1 -> $p2"
Write-Host "gate_leader_takeover = $takeover"
# 30s 内恢复 B
$sw = [System.Diagnostics.Stopwatch]::StartNew()
Start-Process "$root\mes-backend\build\Release\mes-backend.exe" `
    -ArgumentList "config/drogon_config.b.json" -WorkingDirectory "$root\mes-backend" -WindowStyle Hidden
$okB = $false
while ($sw.Elapsed.TotalSeconds -lt 30) {
    try { if ((Invoke-RestMethod -Uri 'http://127.0.0.1:8089/healthz' -TimeoutSec 2).data.status -eq 'ok') { $okB = $true; break } } catch {}
    Start-Sleep 2
}
$sw.Stop()
Write-Host "B recovered in $([math]::Round($sw.Elapsed.TotalSeconds,1))s"
Write-Host "gate_self_heal_30s = $okB"
Write-Host "== 演练汇总: canary=$gate_canary rollback=$gate_rollback self_heal=$okB takeover=$takeover =="
