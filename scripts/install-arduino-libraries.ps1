<# 
Helper script for this repo that runs the Arduino CLI commands the README recommends.
Run this from the repo root: `.\scripts\install-arduino-libraries.ps1`.
#>

Push-Location (Split-Path -Parent $MyInvocation.MyCommand.Definition)
Push-Location ..

$cli = Get-Command arduino-cli -ErrorAction SilentlyContinue
if (-not $cli) {
    Write-Host 'arduino-cli not found; install it from https://arduino.cc/learn/arduino-cli and rerun this script.'
    Pop-Location
    Pop-Location
    exit 1
}

$steps = @(
    @('core', 'update-index'),
    @('lib', 'update-index'),
    @('lib', 'install', 'ESP8266Audio'),
    @('lib', 'install', 'ArduinoJson')
)

foreach ($step in $steps) {
    Write-Host "Running: arduino-cli $($step -join ' ')"
    & arduino-cli @step
    if ($LASTEXITCODE -ne 0) {
        Write-Error "arduino-cli $($step -join ' ') failed ($LASTEXITCODE)."
        Pop-Location
        Pop-Location
        exit $LASTEXITCODE
    }
}

Pop-Location
Pop-Location
