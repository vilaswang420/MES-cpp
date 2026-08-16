# tests/e2e/m2_quality_gate.ps1 — P1-2.4 报工质检门禁 E2E
# 场景: ① 需质检工序无合格/让步检验记录 -> 报工 409 且数量不变
#       ② 创建合格检验 (result=1) 后 -> 报工放行
#       ③ 灰度开关 quality_gate_enabled=false (回退) -> 无检验记录也可报工
#       ④ 开关恢复 true -> 门禁重新生效
# 前置: just dev-up 已启动中间件与迁移 (含 013), mes-backend 运行于 :8088。
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

# 建需质检工单并流转到进行中 (每场景独立工单)
function New-QualityWo([string]$tag, [string]$token) {
    $product = Api "POST" "/api/v1/production/products" @{
        product_code = "QG-P-$tag"; product_name = "门禁产品 $tag"; unit = "PCS"
    } $token
    if ($product.code -ne 200) { throw "建产品失败: $($product.message)" }
    $line = Api "POST" "/api/v1/production/lines" @{
        line_code = "QG-L-$tag"; line_name = "门禁产线 $tag"
    } $token
    if ($line.code -ne 200) { throw "建产线失败" }
    $process = Api "POST" "/api/v1/production/processes" @{
        process_code = "QG-R-$tag"; process_name = "门禁工艺 $tag"; product_id = $product.data.id
        steps = @(@{
            step_seq = 1; step_name = "质检工序"; step_code = "QG-S1-$tag"; quality_check = $true
        })
    } $token
    if ($process.code -ne 200) { throw "建工艺失败: $($process.message)" }
    $wo = Api "POST" "/api/v1/production/work-orders" @{
        product_id = $product.data.id; process_id = $process.data.id
        line_id = $line.data.id; plan_qty = 10
    } $token
    if ($wo.code -ne 200) { throw "建工单失败: $($wo.message)" }
    foreach ($action in @("schedule", "release", "start")) {
        $tr = Api "PUT" "/api/v1/production/work-orders/$($wo.data.id)/$action" $null $token
        if ($tr.code -ne 200) { throw "流转 $action 失败: $($tr.message)" }
    }
    return $wo.data
}

Write-Host "==> admin 登录" -ForegroundColor Cyan
$login = Api "POST" "/api/v1/auth/login" @{ username = "admin"; password = "password" }
if ($login.code -ne 200) { throw "admin 登录失败" }
$tk = $login.data.access_token

# ---- ① 门禁拦截: 需质检工序无检验记录 -> 409 ----
Write-Host "`n== 场景①: 无检验记录 -> 报工被拒 (409) ==" -ForegroundColor Cyan
$wo1 = New-QualityWo "A$suffix" $tk
$r1 = Api "POST" "/api/v1/production/work-orders/$($wo1.id)/report" @{ step_seq = 1; good_qty = 1 } $tk
Assert ($r1.code -eq 409 -and $r1.message -like "*质检*") "报工被拒 409 (提示先质检), 实际 code=$($r1.code) msg=$($r1.message)"
$det1 = Api "GET" "/api/v1/production/work-orders/$($wo1.id)" $null $tk
Assert ($det1.data.completed_qty -eq 0) "被拒后 completed_qty 不变 (=0)"

# ---- ② 创建合格检验 -> 放行 ----
Write-Host "`n== 场景②: 合格检验 (result=1) 后放行 ==" -ForegroundColor Cyan
$insp = Api "POST" "/api/v1/quality/inspections" @{
    inspection_type = 2; work_order_id = $wo1.id; sample_qty = 1; pass_qty = 1
    result = 1; remark = "门禁测试合格检验"
} $tk
Assert ($insp.code -eq 200 -and $insp.data.inspection_no -like "QC-*") "创建合格检验记录"
$r2 = Api "POST" "/api/v1/production/work-orders/$($wo1.id)/report" @{ step_seq = 1; good_qty = 1 } $tk
Assert ($r2.code -eq 200 -and $r2.data.reported -eq 1) "有检验记录后报工放行, code=$($r2.code)"
$det2 = Api "GET" "/api/v1/production/work-orders/$($wo1.id)" $null $tk
Assert ($det2.data.completed_qty -eq 1) "报工落库 completed_qty=1"

# ---- ③ 开关关闭 (回退) -> 无检验记录也放行 ----
Write-Host "`n== 场景③: quality_gate_enabled=false 回退 ==" -ForegroundColor Cyan
$cfgOff = Api "PUT" "/api/v1/system/configs/quality_gate_enabled" @{ config_value = "false" } $tk
Assert ($cfgOff.code -eq 200) "关闭灰度开关"
$wo2 = New-QualityWo "B$suffix" $tk
$r3 = Api "POST" "/api/v1/production/work-orders/$($wo2.id)/report" @{ step_seq = 1; good_qty = 1 } $tk
Assert ($r3.code -eq 200) "开关关闭后无检验记录也放行, code=$($r3.code)"

# ---- ④ 开关恢复 -> 门禁重新生效 ----
Write-Host "`n== 场景④: quality_gate_enabled=true 恢复 ==" -ForegroundColor Cyan
$cfgOn = Api "PUT" "/api/v1/system/configs/quality_gate_enabled" @{ config_value = "true" } $tk
Assert ($cfgOn.code -eq 200) "恢复灰度开关"
$wo3 = New-QualityWo "C$suffix" $tk
$r4 = Api "POST" "/api/v1/production/work-orders/$($wo3.id)/report" @{ step_seq = 1; good_qty = 1 } $tk
Assert ($r4.code -eq 409) "开关恢复后再次拦截, code=$($r4.code)"

# ---- ⑤ 非质检工序不受影响 (工艺 quality_check 默认 false) ----
Write-Host "`n== 场景⑤: 非质检工序不受门禁影响 ==" -ForegroundColor Cyan
$product5 = Api "POST" "/api/v1/production/products" @{
    product_code = "QG-P-D$suffix"; product_name = "非质检产品 $suffix"; unit = "PCS"
} $tk
$line5 = Api "POST" "/api/v1/production/lines" @{
    line_code = "QG-L-D$suffix"; line_name = "非质检产线 $suffix"
} $tk
$process5 = Api "POST" "/api/v1/production/processes" @{
    process_code = "QG-R-D$suffix"; process_name = "非质检工艺 $suffix"; product_id = $product5.data.id
    steps = @(@{ step_seq = 1; step_name = "普通工序"; step_code = "QG-SD-$suffix" })
} $tk
$wo4 = Api "POST" "/api/v1/production/work-orders" @{
    product_id = $product5.data.id; process_id = $process5.data.id
    line_id = $line5.data.id; plan_qty = 10
} $tk
foreach ($action in @("schedule", "release", "start")) {
    Api "PUT" "/api/v1/production/work-orders/$($wo4.data.id)/$action" $null $tk | Out-Null
}
$r5 = Api "POST" "/api/v1/production/work-orders/$($wo4.data.id)/report" @{ step_seq = 1; good_qty = 1 } $tk
Assert ($r5.code -eq 200) "非质检工序报工放行, code=$($r5.code)"

Write-Host "`n结果: PASS=$pass FAIL=$fail"
if ($fail -gt 0) { exit 1 }
