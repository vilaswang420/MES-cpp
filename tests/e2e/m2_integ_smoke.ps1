# m2_integ_smoke.ps1 — ERP/WMS 集成域冒烟 (计划任务 23 / 设计文档 4.10, 7.5, 7.6 节)
# 覆盖: 订单同步与幂等、ERP 订单转工单(含重复 409)、完工回报 Saga 成功路径、
#       WMS 故障注入触发逆序补偿(工单回滚)、熔断器 OPEN 快速失败、
#       冷却后 HALF_OPEN 探测恢复、失败日志人工重发、WMS 领料
# 依赖: scripts/erp_wms_stub.py (端口 9095, 与迁移 009 的 integ_api_configs 种子一致)
# 注: 熔断冷却 30s, 恢复路径需等待一次冷却窗口 (脚本内 Start-Sleep 31)
$ErrorActionPreference = "Stop"
$base = "http://127.0.0.1:8088"
$stubUrl = "http://127.0.0.1:9095"
$pass = 0; $fail = 0
$stubProc = $null

function Check([string]$name, [bool]$cond) {
    if ($cond) { $script:pass++; Write-Host "[PASS] $name" }
    else { $script:fail++; Write-Host "[FAIL] $name" }
}

# HTTP 调用封装: 非 2xx 不抛异常, 返回 {status, obj}
function InvokeApi([string]$method, [string]$url, $bodyObj, [string]$token) {
    $headers = @{}
    if ($token) { $headers["Authorization"] = "Bearer $token" }
    $bodyBytes = $null
    if ($null -ne $bodyObj) {
        $bodyBytes = [Text.Encoding]::UTF8.GetBytes(($bodyObj | ConvertTo-Json -Depth 8 -Compress))
    }
    try {
        if ($bodyBytes) {
            $r = Invoke-RestMethod -Uri $url -Method $method -Headers $headers -ContentType "application/json" -Body $bodyBytes
        } else {
            $r = Invoke-RestMethod -Uri $url -Method $method -Headers $headers
        }
        return @{ status = 200; obj = $r }
    } catch [System.Net.WebException] {
        $resp = $_.Exception.Response
        if ($null -eq $resp) { throw }
        $code = [int]$resp.StatusCode
        # PS 5.1: 异常时响应流已被 cmdlet 消费, body 在 ErrorDetails.Message
        $txt = ""
        if ($_.ErrorDetails -and $_.ErrorDetails.Message) {
            $txt = $_.ErrorDetails.Message
        } else {
            try {
                $reader = New-Object IO.StreamReader($resp.GetResponseStream())
                $txt = $reader.ReadToEnd()
            } catch {}
        }
        $obj = $null
        try { $obj = $txt | ConvertFrom-Json } catch {}
        return @{ status = $code; obj = $obj }
    }
}

function Psql([string]$sql) {
    return ((docker exec mes-postgres psql -U mes -d mes -t -A -c $sql) | Out-String).Trim()
}

function StubControl($bodyObj) {
    return InvokeApi "POST" "$stubUrl/__control" $bodyObj $null
}

try {
    # ---- 启动/复用 ERP/WMS 桩 ----
    $probe = $null
    try { $probe = InvokeApi "GET" "$stubUrl/__control/state" $null $null } catch {}
    if ($null -eq $probe -or $probe.status -ne 200) {
        $stubProc = Start-Process -FilePath "python" `
            -ArgumentList "scripts/erp_wms_stub.py" -WorkingDirectory (Get-Location) `
            -PassThru -WindowStyle Hidden
        $ok = $false
        for ($i = 0; $i -lt 20; $i++) {
            Start-Sleep -Milliseconds 500
            try {
                $p = InvokeApi "GET" "$stubUrl/__control/state" $null $null
                if ($p.status -eq 200) { $ok = $true; break }
            } catch {}
        }
        Check "ERP/WMS 桩启动 (端口 9095)" $ok
    } else {
        Check "ERP/WMS 桩已在运行 (端口 9095)" $true
    }
    [void](StubControl @{ mode = "normal" })

    # ---- 重置集成域测试数据 (保证脚本可重复运行) ----
    [void](Psql "DELETE FROM integ_sync_logs")
    [void](Psql "DELETE FROM integ_wms_inventory")
    [void](Psql "DELETE FROM integ_erp_orders WHERE erp_order_no LIKE 'ERP-STUB-%'")

    # ---- admin 登录 ----
    $login = InvokeApi "POST" "$base/api/v1/auth/login" @{ username = "admin"; password = "password" } $null
    $token = ""
    if ($login.status -eq 200) { $token = $login.obj.data.access_token }
    Check "admin 登录取 token" (-not [string]::IsNullOrEmpty($token))

    # ---- 主数据准备: 产品 STUB-P1 + 工艺路线 + 绑定 ----
    $prods = InvokeApi "GET" "$base/api/v1/production/products" $null $token
    $productId = 0
    foreach ($p in $prods.obj.data.list) {
        if ($p.product_code -eq "STUB-P1") { $productId = $p.id }
    }
    if ($productId -eq 0) {
        $cp = InvokeApi "POST" "$base/api/v1/production/products" `
            @{ product_code = "STUB-P1"; product_name = "集成桩产品"; unit = "PCS" } $token
        if ($cp.status -eq 200) { $productId = $cp.obj.data.id }
    }
    Check "产品 STUB-P1 建档" ($productId -gt 0)

    $ts = Get-Date -Format "HHmmss"
    $proc = InvokeApi "POST" "$base/api/v1/production/processes" @{
        process_code = "STUB-PROC-$ts"; process_name = "集成桩工艺"; product_id = $productId
        steps = @(@{ step_seq = 1; step_name = "桩组装"; step_code = "STUB-ASM"; std_cycle_time = 60 })
    } $token
    $processId = 0
    if ($proc.status -eq 200) { $processId = $proc.obj.data.id }
    Check "工艺路线建档" ($processId -gt 0)
    [void](Psql "UPDATE prod_products SET process_id = $processId WHERE id = $productId")

    # ---- 1. ERP 订单同步 (首次全新增) ----
    $sync1 = InvokeApi "POST" "$base/api/v1/integration/erp/sync-orders" `
        @{ start_date = "2026-08-01"; end_date = "2026-08-31" } $token
    Check "订单同步 total_synced=5" (($sync1.status -eq 200) -and ($sync1.obj.data.total_synced -eq 5))
    Check "订单同步 new_orders=5" ($sync1.obj.data.new_orders -eq 5)

    # ---- 2. 幂等: 重复同步全部走更新 ----
    $sync2 = InvokeApi "POST" "$base/api/v1/integration/erp/sync-orders" `
        @{ start_date = "2026-08-01"; end_date = "2026-08-31" } $token
    Check "重复同步幂等 updated=5/new=0" (($sync2.obj.data.new_orders -eq 0) -and ($sync2.obj.data.updated_orders -eq 5))

    # ---- 3. 转工单 + 重复转 409 ----
    $erpId1 = [int64](Psql "SELECT id FROM integ_erp_orders WHERE erp_order_no = 'ERP-STUB-0001' AND status = 0 ORDER BY id LIMIT 1")
    $conv1 = InvokeApi "POST" "$base/api/v1/integration/erp/$erpId1/convert" @{} $token
    $wo1 = 0
    if ($conv1.status -eq 200) { $wo1 = $conv1.obj.data.work_order_id }
    Check "ERP 订单转工单成功" ($wo1 -gt 0)
    $conv1b = InvokeApi "POST" "$base/api/v1/integration/erp/$erpId1/convert" @{} $token
    Check "重复转工单返回 409" ($conv1b.status -eq 409)

    # ---- 4. 工单流转至进行中 (status=3) ----
    foreach ($act in @("schedule", "release", "start")) {
        [void](InvokeApi "PUT" "$base/api/v1/production/work-orders/$wo1/$act" @{} $token)
    }
    $det1 = InvokeApi "GET" "$base/api/v1/production/work-orders/$wo1" $null $token
    Check "工单 1 开工后 status=3" ($det1.obj.data.status -eq 3)

    # ---- 5. 完工回报 Saga 成功路径: 本地完工 + ERP 回报 + WMS 入库 ----
    $saga1 = InvokeApi "POST" "$base/api/v1/integration/erp/report" @{ work_order_id = $wo1 } $token
    Check "Saga 成功 saga=completed" (($saga1.status -eq 200) -and ($saga1.obj.data.saga -eq "completed"))
    $det1b = InvokeApi "GET" "$base/api/v1/production/work-orders/$wo1" $null $token
    Check "Saga 成功后工单 status=5" ($det1b.obj.data.status -eq 5)

    # ---- 6. Saga 补偿路径: WMS 故障注入 -> 502 + 工单回滚 ----
    $erpId2 = [int64](Psql "SELECT id FROM integ_erp_orders WHERE erp_order_no = 'ERP-STUB-0002' AND status = 0 ORDER BY id LIMIT 1")
    $conv2 = InvokeApi "POST" "$base/api/v1/integration/erp/$erpId2/convert" @{} $token
    $wo2 = 0
    if ($conv2.status -eq 200) { $wo2 = $conv2.obj.data.work_order_id }
    foreach ($act in @("schedule", "release", "start")) {
        [void](InvokeApi "PUT" "$base/api/v1/production/work-orders/$wo2/$act" @{} $token)
    }
    [void](StubControl @{ mode = "fail_wms"; count = 20 })
    $saga2 = InvokeApi "POST" "$base/api/v1/integration/erp/report" @{ work_order_id = $wo2 } $token
    Check "WMS 故障时 Saga 返回 502" ($saga2.status -eq 502)
    $det2 = InvokeApi "GET" "$base/api/v1/production/work-orders/$wo2" $null $token
    Check "补偿后工单回滚 status=3" ($det2.obj.data.status -eq 3)

    # ---- 7. 熔断器: 连续失败 -> OPEN 快速失败 ----
    # saga2 已记 3 次 WMS 失败 (retry_count=3); stock-in 再记 3 次 -> 达阈值 5 -> OPEN
    # 注: 桩的故障注入只影响 /wms/stock-in, 领料始终成功, 故熔断验证走 stock-in
    $si1 = InvokeApi "POST" "$base/api/v1/integration/wms/stock-in" `
        @{ material_code = "STUB-P1"; quantity = 10; work_order_id = $wo2 } $token
    Check "熔断前 stock-in 仍真实外呼 (502 来自对端)" ($si1.status -eq 502)
    $si2 = InvokeApi "POST" "$base/api/v1/integration/wms/stock-in" `
        @{ material_code = "STUB-P1"; quantity = 10; work_order_id = $wo2 } $token
    $msg2 = ""
    if ($si2.obj -and $si2.obj.message) { $msg2 = $si2.obj.message }
    Check "熔断器 OPEN 快速失败" (($si2.status -eq 502) -and ($msg2 -like "*熔断*"))

    # ---- 8. 恢复: 桩转 normal, 等冷却窗口后 HALF_OPEN 探测成功 ----
    [void](StubControl @{ mode = "normal" })
    Write-Host "... 等待熔断冷却窗口 (30s) ..."
    Start-Sleep -Seconds 31
    $logs = InvokeApi "GET" "$base/api/v1/integration/logs?status=0&page_size=50" $null $token
    $failLogId = 0
    foreach ($l in $logs.obj.data.list) {
        if (($l.system_type -eq "WMS") -and ($failLogId -eq 0)) { $failLogId = $l.id }
    }
    Check "存在 WMS 失败日志" ($failLogId -gt 0)
    $retry = InvokeApi "POST" "$base/api/v1/integration/logs/$failLogId/retry" @{} $token
    Check "失败日志重发成功 (半开探测恢复)" (($retry.status -eq 200) -and ($retry.obj.data.status -eq 1))

    # ---- 9. 熔断器恢复 CLOSED: 领料正常成功 ----
    $pick3 = InvokeApi "POST" "$base/api/v1/integration/wms/pick-request" `
        @{ material_code = "STUB-P1"; quantity = 5; work_order_id = $wo2 } $token
    Check "熔断恢复后领料成功" (($pick3.status -eq 200) -and ($pick3.obj.data.sync_log_id -gt 0))

    # ---- 10. 同步日志查询 ----
    $logsAll = InvokeApi "GET" "$base/api/v1/integration/logs?page_size=20" $null $token
    Check "同步日志查询 total>0" (($logsAll.status -eq 200) -and ($logsAll.obj.data.total -gt 0))
} finally {
    [void](StubControl @{ mode = "normal" })
    if ($stubProc) { Stop-Process -Id $stubProc.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host ""
Write-Host "=== m2-integ 冒烟结果: PASS=$pass FAIL=$fail ==="
if ($fail -gt 0) { exit 1 }
