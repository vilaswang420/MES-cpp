# tests/e2e/m2_qc_iot_smoke.ps1 — M2 IoT 18 接口 + 质量域 7 接口冒烟
# 前置: mes-backend 运行于 :8088, 三容器就绪。执行: powershell -File tests/e2e/m2_qc_iot_smoke.ps1
$ErrorActionPreference = "Stop"
# .NET HttpWebRequest 默认对 POST 发 Expect: 100-continue, 与服务端竞态会导致偶发空体 400
[System.Net.ServicePointManager]::Expect100Continue = $false
$base = if ($env:MES_API_BASE) { $env:MES_API_BASE } else { "http://127.0.0.1:8088" }
$pass = 0; $fail = 0

function Api([string]$method, [string]$path, $body = $null, [string]$token = "") {
    $headers = @{ "Content-Type" = "application/json" }
    if ($token) { $headers["Authorization"] = "Bearer $token" }
    # 注意: @{} | ConvertTo-Json 得到空串, 需兜底为 {}, 否则服务端按空体判 400
    $json = if ($null -ne $body) { $b = $body | ConvertTo-Json -Depth 8 -Compress; if ([string]::IsNullOrWhiteSpace($b)) { "{}" } else { $b } } else { $null }
    try {
        if ($null -ne $json) {
            $resp = Invoke-RestMethod -Uri "$base$path" -Method $method -Headers $headers -Body ([System.Text.Encoding]::UTF8.GetBytes($json))
        } else {
            $resp = Invoke-RestMethod -Uri "$base$path" -Method $method -Headers $headers
        }
        return $resp
    } catch {
        # PS 5.1 下非 2xx 时异常流常已被读空, 优先用 ErrorDetails.Message 取响应体
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

$suffix = Get-Date -Format "HHmmss"

# ---- admin 登录 ----
$login = Api "POST" "/api/v1/auth/login" @{ username = "admin"; password = "password" }
Assert ($login.code -eq 200) "admin 登录"
$tk = $login.data.access_token

Write-Host "`n== IoT 域 =="
# 设备 CRUD
$dev = Api "POST" "/api/v1/iot/devices" @{ device_code = "SMK-DEV-$suffix"; device_name = "冒烟设备"; protocol = "modbus_tcp"; status = 1 } $tk
Assert ($dev.code -eq 200 -and $dev.data.id -gt 0) "新增设备"
$devId = $dev.data.id
$list = Api "GET" "/api/v1/iot/devices?keyword=SMK-DEV-$suffix" $null $tk
Assert ($list.code -eq 200 -and $list.data.total -ge 1) "设备列表(关键字)"
$dup = Api "POST" "/api/v1/iot/devices" @{ device_code = "SMK-DEV-$suffix"; device_name = "重复"; protocol = "modbus_tcp" } $tk
Assert ($dup.code -eq 409) "设备编码冲突 -> 409"
$upd = Api "PUT" "/api/v1/iot/devices/$devId" @{ device_name = "冒烟设备-改" } $tk
Assert ($upd.code -eq 200) "修改设备"
$det = Api "GET" "/api/v1/iot/devices/$devId" $null $tk
Assert ($det.code -eq 200 -and $det.data.device_name -eq "冒烟设备-改") "设备详情"
$st = Api "GET" "/api/v1/iot/devices/$devId/status" $null $tk
Assert ($st.code -eq 200) "设备状态"
# 传感器
$sen = Api "POST" "/api/v1/iot/devices/$devId/sensors" @{ sensor_code = "SMK-S1-$suffix"; sensor_name = "温度"; data_type = "FLOAT"; unit = "C"; alarm_high = 80 } $tk
Assert ($sen.code -eq 200 -and $sen.data.id -gt 0) "新增传感器"
$senId = $sen.data.id
$sl = Api "GET" "/api/v1/iot/devices/$devId/sensors" $null $tk
Assert ($sl.code -eq 200 -and $sl.data.Count -ge 1) "传感器列表"
# 数据查询 (无数据也须 200)
$rt = Api "GET" "/api/v1/iot/devices/$devId/realtime-data" $null $tk
Assert ($rt.code -eq 200) "实时数据查询"
$his = Api "GET" "/api/v1/iot/sensors/$senId/history?interval=5m&agg=avg" $null $tk
Assert ($his.code -eq 200) "历史数据查询"
$bad = Api "GET" "/api/v1/iot/sensors/$senId/history?interval=7x" $null $tk
Assert ($bad.code -eq 400) "非法 interval -> 400"
# 告警
$al = Api "GET" "/api/v1/iot/alerts?page=1&page_size=5" $null $tk
Assert ($al.code -eq 200) "告警列表"
$ack404 = Api "PUT" "/api/v1/iot/alerts/999999999/acknowledge" @{} $tk
Assert ($ack404.code -eq 404) "确认不存在告警 -> 404"
# 指令下发 (outbox)
$cmd = Api "POST" "/api/v1/iot/devices/$devId/command" @{ command = "reboot"; params = @{ delay = 1 } } $tk
Assert ($cmd.code -eq 200 -and $cmd.data.queued -eq $true) "指令下发入队"
# 采集任务
$task = Api "POST" "/api/v1/iot/tasks" @{ task_code = "SMK-T-$suffix"; task_name = "冒烟任务"; protocol = "modbus_tcp"; interval_ms = 1000; device_ids = @($devId) } $tk
Assert ($task.code -eq 200 -and $task.data.id -gt 0) "新增采集任务"
$taskId = $task.data.id
$tl = Api "GET" "/api/v1/iot/tasks" $null $tk
Assert ($tl.code -eq 200 -and $tl.data.total -ge 1) "采集任务列表"
$tg = Api "PUT" "/api/v1/iot/tasks/$taskId/toggle" @{ enabled = $false } $tk
Assert ($tg.code -eq 200 -and $tg.data.enabled -eq $false) "启停采集任务"
$tu = Api "PUT" "/api/v1/iot/tasks/$taskId" @{ task_name = "冒烟任务-改" } $tk
Assert ($tu.code -eq 200) "修改采集任务"
$td = Api "DELETE" "/api/v1/iot/tasks/$taskId" $null $tk
Assert ($td.code -eq 200) "删除采集任务"
$dd = Api "DELETE" "/api/v1/iot/devices/$devId" $null $tk
Assert ($dd.code -eq 200) "删除设备(软删)"

Write-Host "`n== 质量域 =="
$stds = Api "GET" "/api/v1/quality/standards" $null $tk
Assert ($stds.code -eq 200) "检验标准列表"
# 创建检验 (含缺陷明细)
$insp = Api "POST" "/api/v1/quality/inspections" @{
    inspection_type = 2; sample_qty = 10; pass_qty = 8; defect_qty = 2;
    remark = "冒烟检验";
    defects = @(
        @{ defect_code = "D-SCRATCH"; defect_name = "划伤"; defect_category = "外观"; quantity = 1; severity = 1 },
        @{ defect_code = "D-DIM"; defect_name = "尺寸超差"; defect_category = "尺寸"; quantity = 1; severity = 3 }
    )
} $tk
Assert ($insp.code -eq 200 -and $insp.data.inspection_no -like "QC-*") "创建检验记录(含缺陷)"
$inspId = $insp.data.id
$badInsp = Api "POST" "/api/v1/quality/inspections" @{ inspection_type = 9 } $tk
Assert ($badInsp.code -eq 400) "非法 inspection_type -> 400"
$il = Api "GET" "/api/v1/quality/inspections?result=2" $null $tk
Assert ($il.code -eq 200 -and $il.data.total -ge 1) "检验记录列表(按结果)"
$det2 = Api "GET" "/api/v1/quality/inspections/$inspId" $null $tk
Assert ($det2.code -eq 200 -and $det2.data.defects.Count -eq 2) "检验详情(含 2 条缺陷)"
$det404 = Api "GET" "/api/v1/quality/inspections/999999999" $null $tk
Assert ($det404.code -eq 404) "不存在检验 -> 404"
$dl = Api "GET" "/api/v1/quality/defects?disposition=0&category=外观" $null $tk
Assert ($dl.code -eq 200 -and $dl.data.total -ge 1) "缺陷列表(类别+状态)"
$defId = $dl.data.list[0].id
$disp = Api "PUT" "/api/v1/quality/defects/$defId/disposition" @{ disposition = 1; root_cause = "搬运磕碰"; corrective_action = "加装防护" } $tk
Assert ($disp.code -eq 200) "缺陷处置(返工)"
$disp2 = Api "PUT" "/api/v1/quality/defects/$defId/disposition" @{ disposition = 3 } $tk
Assert ($disp2.code -eq 404) "重复处置 -> 404"
$badDisp = Api "PUT" "/api/v1/quality/defects/$defId/disposition" @{ disposition = 9 } $tk
Assert ($badDisp.code -eq 400) "非法 disposition -> 400"
$stat = Api "GET" "/api/v1/quality/statistics" $null $tk
Assert ($stat.code -eq 200 -and $stat.data.summary.total -ge 1 -and $stat.data.defect_categories.Count -ge 1) "质量统计"

Write-Host "`n== 权限反例 (fail-closed) =="
# perf_u1 无角色: 全部 403
$pu = Api "POST" "/api/v1/auth/login" @{ username = "perf_u1"; password = "Perf@12345" }
Assert ($pu.code -eq 200) "perf_u1 登录"
$ptk = $pu.data.access_token
$f1 = Api "GET" "/api/v1/iot/devices" $null $ptk
Assert ($f1.code -eq 403) "无角色访问 IoT -> 403"
$f2 = Api "POST" "/api/v1/quality/inspections" @{ inspection_type = 1 } $ptk
Assert ($f2.code -eq 403) "无角色访问质量域 -> 403"

Write-Host "`n结果: PASS=$pass FAIL=$fail"
if ($fail -gt 0) { exit 1 }
