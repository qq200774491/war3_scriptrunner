param(
    [string]$Output,
    [string]$Config,
    [switch]$SkipNative,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$Output) { $Output = Join-Path $projectRoot 'build\package\ScriptRoom-Rust' }
if (!$Config) { $Config = Join-Path $projectRoot '本地测试配置.json' }
$outputRoot = [IO.Path]::GetFullPath($Output)
$configFile = (Resolve-Path -LiteralPath $Config).Path
Push-Location $projectRoot
try {
    if (!$SkipNative) {
        & cmake -S $projectRoot -B (Join-Path $projectRoot 'build') -G 'Visual Studio 17 2022' -A Win32
        if ($LASTEXITCODE) { throw 'Native configure failed' }
        & cmake --build (Join-Path $projectRoot 'build') --config Release
        if ($LASTEXITCODE) { throw 'Native build failed' }
    }
    if (!$SkipBuild) {
        & cargo +stable build --release --locked
        if ($LASTEXITCODE) { throw 'Rust build failed' }
    }
    # 不清空发行目录；升级时保留用户配置、主题和存档。
    $assets = @('scriptrunner.dll', 'tools\launch_game.lua', 'tools\game_control.lua', 'Rust启动器.md', 'rust\gui\assets\tabler\LICENSE', 'rust\gui\assets\README.md')
    $assets += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'map') -Filter '*.lua' | ForEach-Object { 'map\' + $_.Name })
    foreach ($name in @('script_host.exe', 'lua_probe.exe', 'shared_room_native.dll', 'zz_shared_quicktest.dll')) { $assets += 'build\Release\' + $name }
    $manifest = [ordered]@{}
    foreach ($relative in $assets) {
        $source = Join-Path $projectRoot $relative
        $destination = Join-Path $outputRoot $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $manifest[$relative.Replace('\','/')] = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    foreach ($name in @('ScriptRoom.exe', 'ScriptRoomCLI.exe')) {
        $destination = Join-Path $outputRoot $name
        Copy-Item -LiteralPath (Join-Path $projectRoot ('target\release\' + $name)) -Destination $destination -Force
        $manifest[$name] = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $destinationConfig = Join-Path $outputRoot '本地测试配置.json'
    if (!(Test-Path -LiteralPath $destinationConfig)) {
        $settings = Get-Content -LiteralPath $configFile -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($field in @('map','local_entry','cloud_scripts','archives','editor','source_game','game')) {
            $value = $settings.$field
            if (![IO.Path]::IsPathRooted($value)) { $settings.$field = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $configFile) $value)) }
        }
        [IO.File]::WriteAllText($destinationConfig, ($settings | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    }
    [IO.File]::WriteAllText((Join-Path $outputRoot 'package-manifest.json'), ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    & (Join-Path $outputRoot 'ScriptRoomCLI.exe') --check
    if ($LASTEXITCODE) { throw 'Packaged configuration check failed' }
    Write-Output "PACKAGE $outputRoot"
}
finally { Pop-Location }
