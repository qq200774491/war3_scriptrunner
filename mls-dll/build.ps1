# Build mls-dll as 32-bit Windows DLL, then deploy to BOTH KKYDWE plugin dirs:
#   - editor side:    D:\war3Project\KKYDWE\plugin\
#   - war3 game side: D:\war3Project\KKYDWE\plugin\warcraft3\
#
# KKYDWE keeps editor and game plugins in separate directories; the same DLL
# binary is fine for both (DllMain handles the war3 path, exported PluginName +
# Initialize handle the editor path).
#
# Usage:
#   .\build.ps1               # release, copy to both default deploy paths
#   .\build.ps1 -DebugBuild   # debug build (slower, larger; useful for symbols)
#   .\build.ps1 -NoCopy       # build only, do not copy
#   .\build.ps1 -DeployDirs <dir>[,<dir>...]  # override deploy targets

param(
    [switch]$DebugBuild,
    [switch]$NoCopy,
    [string[]]$DeployDirs = @(
        'D:\war3Project\KKYDWE\plugin',
        'D:\war3Project\KKYDWE\plugin\warcraft3'
    )
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $scriptDir

$target      = 'i686-pc-windows-msvc'
$profileName = if ($DebugBuild) { 'debug' } else { 'release' }

$cargoArgs = @('build', '--target', $target)
if (-not $DebugBuild) { $cargoArgs += '--release' }

Write-Host "[mls-dll] cargo $($cargoArgs -join ' ')" -ForegroundColor Cyan
& cargo @cargoArgs
if ($LASTEXITCODE -ne 0) {
    throw "cargo build failed (exit $LASTEXITCODE)"
}

$source = Join-Path $scriptDir "target\$target\$profileName\mls_sim.dll"
if (-not (Test-Path $source)) {
    throw "build output missing: $source"
}

$srcInfo = Get-Item $source
Write-Host "[mls-dll] built: $source ($([math]::Round($srcInfo.Length/1024,1)) KiB)" -ForegroundColor Green

if ($NoCopy) {
    Write-Host "[mls-dll] -NoCopy set, skipping deploy"
    return
}

foreach ($dir in $DeployDirs) {
    if (-not (Test-Path $dir)) {
        Write-Warning "[mls-dll] deploy dir missing, skipping: $dir"
        continue
    }
    $dest = Join-Path $dir 'mls-sim.dll'
    Copy-Item $source $dest -Force
    Write-Host "[mls-dll] deployed -> $dest" -ForegroundColor Green
}
