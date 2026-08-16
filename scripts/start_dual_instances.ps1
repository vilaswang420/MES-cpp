# M3 任务 26: 双实例无状态扩容实测启动脚本 (用后保留为运维参考)
$root = "e:\Work\Development\Projects\Hm_MES\New-MES\mes-backend"
taskkill /F /IM mes-backend.exe 2>$null
Start-Sleep 2
Start-Process "$root\build\Release\mes-backend.exe" -WorkingDirectory $root -WindowStyle Hidden
Start-Process "$root\build\Release\mes-backend.exe" -ArgumentList "config/drogon_config.b.json" -WorkingDirectory $root -WindowStyle Hidden
Start-Sleep 6
$a = try { (Invoke-RestMethod -Uri 'http://127.0.0.1:8088/healthz' -TimeoutSec 5).data.status } catch { "FAIL" }
$b = try { (Invoke-RestMethod -Uri 'http://127.0.0.1:8089/healthz' -TimeoutSec 5).data.status } catch { "FAIL" }
Write-Host "instance A(8088)=$a instance B(8089)=$b"
