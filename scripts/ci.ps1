# Local CI: native unit tests + both target builds. Run before committing.
#   powershell -ExecutionPolicy Bypass -File scripts\ci.ps1
# Native tests need a host g++ on PATH (portable MinGW / w64devkit works).
$ErrorActionPreference = 'Stop'
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$repo = Split-Path $PSScriptRoot -Parent

Write-Host "== native unit tests ==" -ForegroundColor Cyan
& $pio test -e native -d $repo
if ($LASTEXITCODE) { Write-Host "native tests FAILED" -ForegroundColor Red; exit 1 }

Write-Host "== build esp32dev (bench) ==" -ForegroundColor Cyan
& $pio run -e esp32dev -d $repo
if ($LASTEXITCODE) { Write-Host "esp32dev build FAILED" -ForegroundColor Red; exit 1 }

Write-Host "== build esp32dev-mini (car) ==" -ForegroundColor Cyan
& $pio run -e esp32dev-mini -d $repo
if ($LASTEXITCODE) { Write-Host "esp32dev-mini build FAILED" -ForegroundColor Red; exit 1 }

Write-Host "ALL GREEN" -ForegroundColor Green
