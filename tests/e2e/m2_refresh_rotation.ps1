# tests/e2e/m2_refresh_rotation.ps1 — P3-4.3 JWT refresh token 轮换 E2E
# 覆盖: ① refresh 后旧 refresh 重放被拒 (轮换核心); ② 新 refresh 可继续轮换;
#       ③ logout 后当前会话 refresh 不可用 (注销级联)。
# 前置: just dev-up 已启动中间件与迁移, mes-backend 运行于 :8088。
$ErrorActionPreference = "Stop"
$base = if ($env:MES_API_BASE) { $env:MES_API_BASE } else { "http://127.0.0.1:8088" }
$pass = 0; $fail = 0

function Check([string]$name, [bool]$cond) {
    if ($cond) { $script:pass++; Write-Host "[PASS] $name" }
    else { $script:fail++; Write-Host "[FAIL] $name" }
}

function Api([string]$method, [string]$path, $body = $null, [string]$token = "") {
    $headers = @{ "Content-Type" = "application/json" }
    if ($token) { $headers["Authorization"] = "Bearer $token" }
    $json = if ($null -ne $body) { $body | ConvertTo-Json -Depth 8 -Compress } else { $null }
    try {
        if ($null -ne $json) {
            $resp = Invoke-RestMethod -Uri "$base$path" -Method $method -Headers $headers -Body ([System.Text.Encoding]::UTF8.GetBytes($json))
        } else {
            $resp = Invoke-RestMethod -Uri "$base$path" -Method $method -Headers $headers
        }
        return @{ code = 200; data = $resp.data }
    } catch {
        $txt = "$($_.ErrorDetails.Message)"
        $r = $_.Exception.Response
        $httpStatus = if ($r) { [int]$r.StatusCode } else { 0 }
        if ($txt) {
            try { $j = $txt | ConvertFrom-Json; return @{ code = $j.code; msg = $j.message } } catch {}
        }
        if ($httpStatus -gt 0) { return @{ code = $httpStatus; msg = "HTTP $httpStatus" } }
        throw
    }
}

# ---- 1. 登录取初始 refresh ----
Write-Host "==> admin 登录" -ForegroundColor Cyan
$login = Api "POST" "/api/v1/auth/login" @{ username = "admin"; password = "password" }
Check "登录成功" ($login.code -eq 200)
$r1 = $login.data.refresh_token
$a1 = $login.data.access_token
Check "获取 refresh_token" (-not [string]::IsNullOrEmpty($r1))

# ---- 2. 首次 refresh: 轮换出 R2 ----
Write-Host "`n== 首次 refresh (R1 -> R2) ==" -ForegroundColor Cyan
$ref2 = Api "POST" "/api/v1/auth/refresh" @{ refresh_token = $r1 }
Check "refresh(R1) 成功签发 R2" ($ref2.code -eq 200 -and -not [string]::IsNullOrEmpty($ref2.data.refresh_token))
$r2 = $ref2.data.refresh_token
$a2 = $ref2.data.access_token

# ---- 3. 旧 refresh 重放必须被拒 (轮换核心) ----
Write-Host "`n== 旧 refresh 重放 ==" -ForegroundColor Cyan
$replay = Api "POST" "/api/v1/auth/refresh" @{ refresh_token = $r1 }
Check "refresh(R1) 重放被拒 (401)" ($replay.code -eq 401)
$replay2 = Api "POST" "/api/v1/auth/refresh" @{ refresh_token = $r1 }
Check "refresh(R1) 二次重放仍被拒 (401)" ($replay2.code -eq 401)

# ---- 4. 新 refresh 可继续轮换 (R2 -> R3) ----
Write-Host "`n== 新 refresh 继续轮换 ==" -ForegroundColor Cyan
$ref3 = Api "POST" "/api/v1/auth/refresh" @{ refresh_token = $r2 }
Check "refresh(R2) 成功签发 R3" ($ref3.code -eq 200 -and -not [string]::IsNullOrEmpty($ref3.data.refresh_token))
$r3 = $ref3.data.refresh_token
$a3 = $ref3.data.access_token
Check "R2 轮换后重放被拒" ((Api "POST" "/api/v1/auth/refresh" @{ refresh_token = $r2 }).code -eq 401)

# ---- 5. logout 级联: 当前会话作废后 refresh 不可用 ----
Write-Host "`n== logout 级联 ==" -ForegroundColor Cyan
$out = Api "POST" "/api/v1/auth/logout" $null $a3
Check "logout(A3) 成功" ($out.code -eq 200)
$afterLogout = Api "POST" "/api/v1/auth/refresh" @{ refresh_token = $r3 }
Check "logout 后 refresh(R3) 被拒 (401)" ($afterLogout.code -eq 401)

Write-Host "`n结果: PASS=$pass FAIL=$fail"
if ($fail -gt 0) { exit 1 }
