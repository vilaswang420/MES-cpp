# tests/e2e/m2_concurrent_report.ps1 — P2-3.1 报工并发稳定性 E2E
# 场景: 建 plan_qty=100 工单流转至进行中 -> .NET HttpClient 并发 120 次报工 (good_qty=1)
#       -> 断言: 恰好 100 成功且 DB completed_qty=100 (不超报, 原子防超报生效),
#               剩余 20 次被拦截 (409), 满量自动完工 (status=5)。
# 前置: just dev-up 已启动中间件, hms-backend 运行于 :8088。
$ErrorActionPreference = "Stop"
[System.Net.ServicePointManager]::Expect100Continue = $false
$base = if ($env:HMS_API_BASE) { $env:HMS_API_BASE } else { "http://127.0.0.1:8088" }
$suffix = Get-Date -Format "HHmmssfff"
$planQty = 100
$concurrent = 120   # plan + 20 超额
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
    return ((docker exec hms-postgres psql -U hms -d hms -t -A -c $sql) | Out-String).Trim()
}

Write-Host "==> admin 登录" -ForegroundColor Cyan
$login = Api "POST" "/api/v1/auth/login" @{ username = "admin"; password = "password" }
if ($login.code -ne 200) { throw "admin 登录失败" }
$tk = $login.data.access_token

# ---- 0. 建数: 产品/产线/工艺(1 工序)/工单 plan=100, 流转至进行中 ----
Write-Host "`n== 准备: 工单 plan_qty=$planQty 流转至进行中 ==" -ForegroundColor Cyan
$product = Api "POST" "/api/v1/production/products" @{
    product_code = "PC-$suffix"; product_name = "并发报工产品 $suffix"; unit = "PCS"
} $tk
Assert ($product.code -eq 200) "创建产品, code=$($product.code)"
$line = Api "POST" "/api/v1/production/lines" @{ line_code = "LC-$suffix"; line_name = "并发产线 $suffix" } $tk
Assert ($line.code -eq 200) "创建产线, code=$($line.code)"
$process = Api "POST" "/api/v1/production/processes" @{
    process_code = "RC-$suffix"; process_name = "并发工艺 $suffix"; product_id = $product.data.id
    steps = @(@{ step_seq = 1; step_name = "组装"; step_code = "SC1-$suffix" })
} $tk
Assert ($process.code -eq 200) "创建工艺(1 工序), code=$($process.code)"
$wo = Api "POST" "/api/v1/production/work-orders" @{
    product_id = $product.data.id; process_id = $process.data.id; line_id = $line.data.id
    plan_qty = $planQty; priority = 5
} $tk
Assert ($wo.code -eq 200) "创建工单, code=$($wo.code)"
foreach ($action in @("schedule", "release", "start")) {
    $r = Api "PUT" "/api/v1/production/work-orders/$($wo.data.id)/$action" $null $tk
    Assert ($r.code -eq 200) "流转 $action, code=$($r.code)"
}

# ---- 1. 并发 120 报工 (HttpClient + Task.WaitAll, 真并发) ----
Write-Host "`n== 场景: 并发 $concurrent 次报工 (good_qty=1) ==" -ForegroundColor Cyan
$handler = [System.Net.Http.HttpClientHandler]::new()
$handler.ServerCertificateCustomValidationCallback = { $true }
$client = [System.Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(60)
$client.DefaultRequestHeaders.Authorization =
    [System.Net.Http.Headers.AuthenticationHeaderValue]::new("Bearer", $tk)
$uri = "$base/api/v1/production/work-orders/$($wo.data.id)/report"
$tasks = for ($i = 0; $i -lt $concurrent; $i++) {
    $body = [System.Text.Encoding]::UTF8.GetBytes('{"step_seq":1,"good_qty":1}')
    $content = [System.Net.Http.ByteArrayContent]::new($body)
    $content.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::new("application/json")
    $client.PostAsync($uri, $content)
}
[System.Threading.Tasks.Task]::WaitAll($tasks)
$ok = 0; $conflict = 0; $unexpected = @()
for ($i = 0; $i -lt $tasks.Count; $i++) {
    $resp = $tasks[$i].Result
    $code = if ($resp.IsSuccessStatusCode) {
        $json = $resp.Content.ReadAsStringAsync().Result | ConvertFrom-Json
        $json.code
    } else { [int]$resp.StatusCode }
    if ($code -eq 200) { $ok++ }
    elseif ($code -eq 409) { $conflict++ }
    else { $unexpected += "req#$i -> HTTP $([int]$resp.StatusCode) code=$code" }
}
$client.Dispose()
Write-Host "  成功=$ok 超报拦截(409)=$conflict 意外错误=$($unexpected.Count)"
Assert ($ok -eq $planQty) "恰 $planQty 次成功 (满量), 实际=$ok"
Assert ($conflict -eq ($concurrent - $planQty)) "剩余 $(($concurrent - $planQty)) 次被原子拦截 (409), 实际=$conflict"
Assert ($unexpected.Count -eq 0) "无意外错误 (5xx/其他): $($unexpected -join '; ')"

# ---- 2. DB 断言: 不超报 + 自动完工 ----
Write-Host "`n== DB 断言 ==" -ForegroundColor Cyan
$completed = [int64](Psql "SELECT completed_qty FROM prod_work_orders WHERE id = $($wo.data.id)")
Assert ($completed -eq $planQty) "DB completed_qty=$completed == plan $planQty (不超报)"
$status = (Psql "SELECT status FROM prod_work_orders WHERE id = $($wo.data.id)")
Assert ($status -eq "5") "满量自动完工 status=5, 实际=$status"
$opCompleted = (Psql "SELECT completed_qty FROM prod_work_order_operations WHERE work_order_id = $($wo.data.id) AND step_seq = 1")
Assert ($opCompleted -eq "$planQty") "工序 completed_qty=$opCompleted (含 P2-2.11h scrap 列兼容)"

Write-Host "`n结果: PASS=$pass FAIL=$fail"
if ($fail -gt 0) { exit 1 }
