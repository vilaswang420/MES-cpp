$p = $args[0]
$txt = [IO.File]::ReadAllText($p, [Text.Encoding]::UTF8)
[IO.File]::WriteAllText($p, $txt, (New-Object Text.UTF8Encoding $true))
Write-Host "BOM added: $p"
