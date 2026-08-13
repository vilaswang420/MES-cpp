# 报工并发超报测试 (M1 钱袋子逻辑):
#   同一工单最后一件, N 个并发报工会话同时提交 good_qty=1,
#   必须恰好 1 个成功 (行级锁 FOR UPDATE 串行化), 其余全部 409 拒绝;
#   最终 completed_qty 恰好等于 plan_qty, 恰好触发 1 条停采 outbox。
# 前置: just dev-up 已启动中间件与迁移, hms-backend 运行于 :8088。
$ErrorActionPreference = "Stop"
$base = if ($env:HMS_API_BASE) { $env:HMS_API_BASE } else { "http://127.0.0.1:8088" }
$suffix = Get-Random -Minimum 1000 -Maximum 9999
$concurrency = 8

function Invoke-Api {
    param([string]$Method, [string]$Path, $Body = $null, [string]$Token = "")
    $headers = @{ "Content-Type" = "application/json" }
    if ($Token) { $headers["Authorization"] = "Bearer $Token" }
    $json = if ($null -ne $Body) { $Body | ConvertTo-Json -Depth 8 } else { $null }
    $resp = Invoke-RestMethod -Uri "$base$Path" -Method $Method -Headers $headers -Body $json
    if ($resp.code -ne 200) { throw "API 业务错误 [$Method $Path]: code=$($resp.code) msg=$($resp.message)" }
    return $resp.data
}

# ---- 1. 建数: plan_qty=1 的工单流转到进行中 ----
Write-Host "==> 准备 plan_qty=1 的进行中工单" -ForegroundColor Cyan
$login = Invoke-Api -Method POST -Path "/api/v1/auth/login" -Body @{ username = "admin"; password = "password" }
$token = $login.access_token
$product = Invoke-Api -Method POST -Path "/api/v1/production/products" -Token $token -Body @{
    product_code = "CR-P-$suffix"; product_name = "并发测试产品 $suffix"; unit = "PCS"
}
$line = Invoke-Api -Method POST -Path "/api/v1/production/lines" -Token $token -Body @{
    line_code = "CR-L-$suffix"; line_name = "并发测试产线 $suffix"
}
$process = Invoke-Api -Method POST -Path "/api/v1/production/processes" -Token $token -Body @{
    process_code = "CR-R-$suffix"; process_name = "并发工艺 $suffix"; product_id = $product.id
    steps = @(@{ step_seq = 1; step_name = "组装"; step_code = "CR-S1-$suffix" })
}
$wo = Invoke-Api -Method POST -Path "/api/v1/production/work-orders" -Token $token -Body @{
    product_id = $product.id; process_id = $process.id; line_id = $line.id; plan_qty = 1
}
foreach ($action in @("schedule", "release", "start")) {
    Invoke-Api -Method PUT -Path "/api/v1/production/work-orders/$($wo.id)/$action" -Token $token | Out-Null
}
Write-Host "  WO#$($wo.id) $($wo.work_order_no) 已开工 (plan_qty=1)"

# ---- 2. N 路并发同时报最后一件 ----
Write-Host "==> $concurrency 路并发报工 (step_seq=1, good_qty=1)" -ForegroundColor Cyan
$jobs = @()
for ($i = 0; $i -lt $concurrency; $i++) {
    $jobs += Start-Job -ScriptBlock {
        param($Base, $WoId, $Token)
        try {
            $headers = @{ "Content-Type" = "application/json"; "Authorization" = "Bearer $Token" }
            $resp = Invoke-RestMethod -Uri "$Base/api/v1/production/work-orders/$WoId/report" `
                -Method POST -Headers $headers -Body (@{ step_seq = 1; good_qty = 1 } | ConvertTo-Json)
            # 返回纯字符串 (Job 反序列化 PSCustomObject 存在丢结果的环境差异)
            return "OK:$($resp.code)"
        } catch {
            # PS 5.1 Job 环境下部分异常读不到响应体: 优先从异常消息正则提取状态码,
            # 响应体读取作兑底
            $code = 0
            if ($_.Exception.Message -match '\((\d{3})\)') { $code = [int]$Matches[1] }
            try {
                $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
                $parsed = $reader.ReadToEnd() | ConvertFrom-Json
                if ($parsed.code) { $code = $parsed.code }
            } catch {}
            return "FAIL:$code"
        }
    } -ArgumentList $base, $wo.id, $token
}
$results = $jobs | Wait-Job | Receive-Job
$jobs | Remove-Job

$okCodes = @($results | Where-Object { $_ -like 'OK*' })
$failCodes = @($results | ForEach-Object { if ($_ -like 'FAIL:*') { [int]($_ -replace 'FAIL:', '') } })
Write-Host "  成功 $($okCodes.Count) / 拒绝 $($failCodes.Count) (共 $($results.Count) 路)"
foreach ($c in $failCodes) { Write-Host "    [拒绝] code=$c" }

# ---- 3. 断言: 恰好 1 成功; 其余必须是 409 (超报/状态冲突), 不允许 5xx ----
if ($okCodes.Count -ne 1) { throw "并发超报防护失效: 成功数应为 1, 实际 $($okCodes.Count)" }
foreach ($c in $failCodes) {
    if ($c -ne 409) { throw "并发拒绝应为 409 冲突, 实际 $c" }
}

# ---- 4. 断言: completed_qty 恰好等于 plan_qty, 且已自动完工 ----
$detail = Invoke-Api -Method GET -Path "/api/v1/production/work-orders/$($wo.id)" -Token $token
if ($detail.completed_qty -ne 1) { throw "completed_qty 应为 1, 实际 $($detail.completed_qty)" }
if ($detail.status -ne 5) { throw "满量后应自动完工 (status=5), 实际 $($detail.status)" }
Write-Host "  completed_qty=1, status=5 (自动完工)"

# ---- 5. 断言: 恰好 1 条停采 outbox (不允许重复触发) ----
$outboxCount = docker exec hms-postgres psql -U hms -d hms -t -A -c `
    "SELECT COUNT(*) FROM mq_outbox WHERE routing_key='cmd.stop_collection' AND payload LIKE '%$($wo.work_order_no)%';"
if ([int]$outboxCount -ne 1) { throw "停采 outbox 应恰好 1 条, 实际 $outboxCount" }
Write-Host "  停采 outbox 恰好 1 条"

Write-Host "`n并发超报测试全部通过" -ForegroundColor Green
