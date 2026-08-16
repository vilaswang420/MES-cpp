# M1 出口 E2E (计划: M1 出口标准硬门禁), 全程无人工改库:
#   登录 -> 建产品/产线/工艺 -> 建单 -> 排产 -> 下达 -> 开工
#   -> 报工x2 -> 自动完工 -> outbox 标记已投递
#   反例: operator 访问 system:user:list 403; data_scope=1 查不到他人工单; 审计可分页查询
# 前置: just dev-up 已启动中间件与迁移, mes-backend 运行于 :8088。
$ErrorActionPreference = "Stop"
$base = if ($env:MES_API_BASE) { $env:MES_API_BASE } else { "http://127.0.0.1:8088" }
$suffix = Get-Random -Minimum 1000 -Maximum 9999

function Invoke-Api {
    param([string]$Method, [string]$Path, $Body = $null, [string]$Token = "")
    $headers = @{ "Content-Type" = "application/json" }
    if ($Token) { $headers["Authorization"] = "Bearer $Token" }
    $json = if ($null -ne $Body) { $Body | ConvertTo-Json -Depth 8 } else { $null }
    $resp = Invoke-RestMethod -Uri "$base$Path" -Method $Method -Headers $headers -Body $json
    if ($resp.code -ne 200) { throw "API 业务错误 [$Method $Path]: code=$($resp.code) msg=$($resp.message) trace=$($resp.trace_id)" }
    return $resp.data
}

function Invoke-Api-ExpectFail {
    param([string]$Method, [string]$Path, [string]$Token, [int]$ExpectCode)
    try {
        $headers = @{}
        if ($Token) { $headers["Authorization"] = "Bearer $Token" }
        Invoke-RestMethod -Uri "$base$Path" -Method $Method -Headers $headers | Out-Null
        throw "期望失败但成功了: $Method $Path"
    } catch {
        $status = 0
        if ($_.Exception.Response) { $status = [int]$_.Exception.Response.StatusCode }
        $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
        $bodyText = $reader.ReadToEnd()
        $code = 0
        try { $code = ($bodyText | ConvertFrom-Json).code } catch {}
        if ($code -ne $ExpectCode -and $status -ne $ExpectCode) {
            throw "期望 $ExpectCode, 实际 HTTP=$status code=$code body=$bodyText ($Method $Path)"
        }
        Write-Host "  [反例通过] $Method $Path -> $ExpectCode"
    }
}

function Step([string]$Name) { Write-Host "`n==> $Name" -ForegroundColor Cyan }

# ---- 0. 健康检查 ----
Step "healthz"
$hz = Invoke-RestMethod -Uri "$base/healthz"
if ($hz.code -ne 200) { throw "healthz 失败" }

# ---- 1. admin 登录 (seed 密码 password, 仅 dev) ----
Step "admin 登录"
$login = Invoke-Api -Method POST -Path "/api/v1/auth/login" -Body @{ username = "admin"; password = "password" }
$admin = $login.access_token
Write-Host "  token 已获取 (user=$($login.user.username))"

# ---- 2. 建主数据: 产品 / 产线 / 工艺(2 工序) ----
Step "创建产品/产线/工艺"
$product = Invoke-Api -Method POST -Path "/api/v1/production/products" -Token $admin -Body @{
    product_code = "E2E-P-$suffix"; product_name = "E2E 测试产品 $suffix"; unit = "PCS"
}
$line = Invoke-Api -Method POST -Path "/api/v1/production/lines" -Token $admin -Body @{
    line_code = "E2E-L-$suffix"; line_name = "E2E 测试产线 $suffix"
}
$process = Invoke-Api -Method POST -Path "/api/v1/production/processes" -Token $admin -Body @{
    process_code = "E2E-R-$suffix"; process_name = "E2E 工艺 $suffix"; product_id = $product.id
    steps = @(
        @{ step_seq = 1; step_name = "组装"; step_code = "E2E-S1-$suffix" },
        @{ step_seq = 2; step_name = "测试"; step_code = "E2E-S2-$suffix" }
    )
}
Write-Host "  product=$($product.id) line=$($line.id) process=$($process.id) steps=$($process.steps)"

# ---- 3. 建单 -> 排产 -> 下达 -> 开工 ----
Step "创建工单并流转至进行中"
$wo = Invoke-Api -Method POST -Path "/api/v1/production/work-orders" -Token $admin -Body @{
    product_id = $product.id; process_id = $process.id; line_id = $line.id; plan_qty = 10; priority = 5
}
$woId = $wo.id
if ($wo.status -ne 0) { throw "新工单初始状态应为 0(待排产), 实际 $($wo.status)" }
$s1 = Invoke-Api -Method PUT -Path "/api/v1/production/work-orders/$woId/schedule" -Token $admin
if ($s1.status -ne 1) { throw "排产后应为 1, 实际 $($s1.status)" }
$s2 = Invoke-Api -Method PUT -Path "/api/v1/production/work-orders/$woId/release" -Token $admin
if ($s2.status -ne 2) { throw "下达后应为 2, 实际 $($s2.status)" }
$s3 = Invoke-Api -Method PUT -Path "/api/v1/production/work-orders/$woId/start" -Token $admin
if ($s3.status -ne 3) { throw "开工后应为 3, 实际 $($s3.status)" }
Write-Host "  WO#$woId $($wo.work_order_no) 0->1->2->3 流转成功"

# ---- 4. 报工 x2 -> 自动完工 ----
Step "报工 x2 (工序 1: 5 + 5 => 满量自动完工)"
$r1 = Invoke-Api -Method POST -Path "/api/v1/production/work-orders/$woId/report" -Token $admin -Body @{
    step_seq = 1; good_qty = 5
}
if ($r1.finished) { throw "第一次报工不应完工" }
$r2 = Invoke-Api -Method POST -Path "/api/v1/production/work-orders/$woId/report" -Token $admin -Body @{
    step_seq = 1; good_qty = 5
}
if (-not $r2.finished) { throw "满量报工后应自动完工" }
$detail = Invoke-Api -Method GET -Path "/api/v1/production/work-orders/$woId" -Token $admin
if ($detail.status -ne 5) { throw "完工后状态应为 5, 实际 $($detail.status)" }
Write-Host "  自动完工确认: status=$($detail.status) completed=$($detail.completed_qty)"

# ---- 5. outbox 已投递 (投递器每 2s 一轮, 最多等 15s) ----
Step "验证 mq_outbox 已投递"
$dispatched = $false
for ($i = 0; $i -lt 15; $i++) {
    $outbox = docker exec mes-postgres psql -U mes -d mes -t -A -c "SELECT COUNT(*) FROM mq_outbox WHERE routing_key='cmd.stop_collection' AND status = 1 AND sent_at IS NOT NULL AND payload LIKE '%$($wo.work_order_no)%';"
    if ([int]$outbox -ge 1) { $dispatched = $true; break }
    Start-Sleep -Seconds 1
}
if (-not $dispatched) { throw "mq_outbox 停采指令未在 15s 内标记已投递" }
Write-Host "  outbox 停采指令已投递"

# ---- 6. 权限反例: operator 访问用户列表 403 ----
Step "权限反例: operator 访问 system:user:list 应 403"
$opUser = Invoke-Api -Method POST -Path "/api/v1/system/users" -Token $admin -Body @{
    username = "e2e_op_$suffix"; password = "E2e@12345"; real_name = "E2E 操作员"; employee_no = "E2E$suffix"
}
$roles = Invoke-Api -Method GET -Path "/api/v1/system/roles?page=1&page_size=50" -Token $admin
$opRole = $roles.list | Where-Object { $_.role_code -eq "operator" } | Select-Object -First 1
Invoke-Api -Method PUT -Path "/api/v1/system/users/$($opUser.id)/roles" -Token $admin -Body @{
    role_ids = @($opRole.id)
}
$opLogin = Invoke-Api -Method POST -Path "/api/v1/auth/login" -Body @{
    username = "e2e_op_$suffix"; password = "E2e@12345"
}
$opToken = $opLogin.access_token
Invoke-Api-ExpectFail -Method GET -Path "/api/v1/system/users?page=1" -Token $opToken -ExpectCode 403

# ---- 7. data_scope 反例: operator(scope=1) 查不到他人工单 ----
Step "data_scope 反例: operator 查 admin 创建的工单应 404"
Invoke-Api-ExpectFail -Method GET -Path "/api/v1/production/work-orders/$woId" -Token $opToken -ExpectCode 404

# ---- 8. 审计可分页查询 + 过滤 (P1-2.8) ----
Step "审计日志分页查询"
$audit = Invoke-Api -Method GET -Path "/api/v1/system/audit-logs?page=1&page_size=10" -Token $admin
if ($audit.total -lt 1) { throw "审计日志为空" }
Write-Host "  审计记录 total=$($audit.total)"

Step "审计日志时间范围过滤 (created_at 触发分区裁剪)"
$start = [uri]::EscapeDataString((Get-Date).AddDays(-1).ToString("yyyy-MM-ddTHH:mm:sszzz"))
$auditRange = Invoke-Api -Method GET -Path "/api/v1/system/audit-logs?page=1&page_size=10&start_time=$start" -Token $admin
if ($auditRange.total -lt 1) { throw "时间范围过滤应命中当日审计记录" }
$auditAll = Invoke-Api -Method GET -Path "/api/v1/system/audit-logs?page=1&page_size=10&start_time=2099-01-01T00:00:00%2B08:00" -Token $admin
if ($auditAll.total -ne 0) { throw "未来时间起点应返回空 (total=$($auditAll.total))" }

Step "审计日志组合过滤 (module + response_code)"
$auditComb = Invoke-Api -Method GET -Path "/api/v1/system/audit-logs?page=1&page_size=10&module=auth&response_code=200" -Token $admin
if ($auditComb.total -lt 1) { throw "组合过滤应命中 login 审计 (module=auth&response_code=200)" }

Write-Host "`nM1 E2E 全部通过" -ForegroundColor Green
