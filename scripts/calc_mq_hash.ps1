# 计算 RabbitMQ definitions 用 password_hash (salt 4字节 + sha256(salt+pwd), hex)
$ErrorActionPreference = "Stop"
$pwdText = "hms_dev_pwd"
$saltBytes = 1..4 | ForEach-Object { Get-Random -Maximum 256 }
$salt = ($saltBytes | ForEach-Object { $_.ToString('x2') }) -join ''
$inner = "printf '${salt}${pwdText}' | sha256sum | cut -d' ' -f1"
$digest = docker exec hms-rabbitmq sh -c $inner
if (-not $digest) { throw "容器内计算哈希失败" }
$hash = "$salt$digest"
Write-Host "HASH=$hash"
