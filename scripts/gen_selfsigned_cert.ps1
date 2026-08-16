# Nginx TLS 自签证书生成 (M3 任务 24; 生产应替换为 CA 签发证书)
# 产物: deploy/nginx/certs/mes.crt + mes.key (被 nginx.conf 引用)
$ErrorActionPreference = 'Stop'
$certDir = Join-Path $PSScriptRoot '..\deploy\nginx\certs'
New-Item -ItemType Directory -Force -Path $certDir | Out-Null

# 定位 openssl (PATH 或 Git for Windows 内置)
$openssl = (Get-Command openssl -ErrorAction SilentlyContinue).Source
if (-not $openssl) {
    $openssl = 'C:\Program Files\Git\usr\bin\openssl.exe'
}
if (-not (Test-Path $openssl)) { throw "未找到 openssl, 请安装 Git for Windows 或 OpenSSL" }

# 一条命令生成 10 年自签证书
& $openssl req -x509 -nodes -newkey rsa:2048 -days 3650 `
    -keyout "$certDir\mes.key" -out "$certDir\mes.crt" `
    -subj "/CN=mes.local" `
    -addext "subjectAltName=DNS:localhost,DNS:mes.local,IP:127.0.0.1"

Write-Host "证书已生成: $certDir\mes.crt / mes.key"
