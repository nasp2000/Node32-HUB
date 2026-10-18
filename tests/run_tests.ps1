#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Run the Node32-HUB HTTP integration test suite.
.DESCRIPTION
    Tests require a running Node32-HUB device on the network.
    By default connects to http://node32-hub.local.
.PARAMETER Host
    Device hostname or IP (default: $env:NODE32_HOST or "node32-hub.local")
.PARAMETER Auth
    HTTP Basic Auth as "user:pass" (default: $env:NODE32_AUTH or none)
.PARAMETER Timeout
    Request timeout in seconds (default: $env:NODE32_TIMEOUT or 10)
.PARAMETER XUnit
    Generate xUnit XML report
.PARAMETER Verbose
    Verbose pytest output
.EXAMPLE
    .\run_tests.ps1 -Host 192.168.1.100
.EXAMPLE
    .\run_tests.ps1 -Auth "admin:secret" -Verbose
#>

param(
    [string]$Host = "",
    [string]$Auth = "",
    [double]$Timeout = 0,
    [switch]$XUnit,
    [switch]$Verbose
)

$ScriptDir = Split-Path -Parent $PSCommandPath
pushd $ScriptDir

# Check dependencies
try {
    python -c "import pytest" 2>$null | Out-Null
} catch {
    Write-Error "pytest not found. Install with: pip install -r requirements.txt"
    popd
    exit 1
}

# Build pytest args
$args = @()

if ($Host) { $args += "--host"; $args += $Host }
if ($Auth) { $args += "--auth"; $args += $Auth }
if ($Timeout -gt 0) { $args += "--timeout"; $args += "$Timeout" }
if ($XUnit) {
    New-Item -ItemType Directory -Force -Path "reports" | Out-Null
    $args += "--junitxml=reports/test-results.xml"
}
if ($Verbose) { $args += "-v" }

$args += "-v"   # always at least verbose

Write-Host "Node32-HUB Test Runner" -ForegroundColor Cyan
Write-Host "======================" -ForegroundColor Cyan
Write-Host "Running tests against device..."

# Run
python -m pytest @args

$exitCode = $LASTEXITCODE
popd
exit $exitCode
