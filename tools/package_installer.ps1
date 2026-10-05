param(
    [string]$Output,
    [string]$Compiler,
    [string]$Version,
    [switch]$SkipNative,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$Output) { $Output = Join-Path $projectRoot 'build\package\installer' }
$outputRoot = [IO.Path]::GetFullPath($Output)
$payloadRoot = Join-Path $projectRoot 'build\installer\payload'

function Find-InstallerCompiler {
    if ($Compiler) { return (Resolve-Path -LiteralPath $Compiler).Path }
    $installed = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($installed) { return $installed.Source }
    foreach ($candidate in @(
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 7\ISCC.exe',
        (Join-Path $projectRoot 'build\tools\inno-setup-6.7.3\ISCC.exe')
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    # 构建工具使用官方固定版本，便携安装到 build，不注册系统安装项。
    $toolsRoot = Join-Path $projectRoot 'build\tools'
    New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
    $download = Join-Path $toolsRoot 'innosetup-6.7.3.exe'
    Invoke-WebRequest -Uri 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' -OutFile $download
    $signature = Get-AuthenticodeSignature -LiteralPath $download
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Pyrsys B\.V\.') {
        throw 'Inno Setup download signature verification failed'
    }
    $compilerRoot = Join-Path $toolsRoot 'inno-setup-6.7.3'
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/CURRENTUSER', '/PORTABLE=1', '/NOICONS', ('/DIR="' + $compilerRoot + '"'))
    $process = Start-Process -FilePath $download -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Compiler setup failed: $($process.ExitCode)" }
    return (Resolve-Path -LiteralPath (Join-Path $compilerRoot 'ISCC.exe')).Path
}

$compilerPath = Find-InstallerCompiler
if (!$Version) {
    $manifestText = Get-Content -LiteralPath (Join-Path $projectRoot 'Cargo.toml') -Raw -Encoding UTF8
    $match = [regex]::Match($manifestText, '(?m)^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"')
    if (!$match.Success) { throw 'Cannot read ScriptRoom package version' }
    $Version = $match.Groups[1].Value
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must use major.minor.patch' }

& (Join-Path $PSScriptRoot 'package_rust.ps1') -Output $payloadRoot -SkipNative:$SkipNative -SkipBuild:$SkipBuild
# 发行包只读取正式文件清单，排除本机配置、日志、连接信息、地图和存档。
$manifest = Get-Content -LiteralPath (Join-Path $payloadRoot 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$entries = @()
foreach ($property in $manifest.PSObject.Properties) {
    $relative = $property.Name.Replace('/', '\')
    $source = [IO.Path]::GetFullPath((Join-Path $payloadRoot $relative))
    if (!$source.StartsWith($payloadRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $relative.Contains('"')) {
        throw "Invalid packaged asset path: $relative"
    }
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $property.Value) { throw "Packaged asset changed: $relative" }
    $parent = Split-Path -Parent $relative
    $destination = if ($parent) { '{app}\' + $parent } else { '{app}' }
    $entries += 'Source: "{#PayloadDir}\' + $relative + '"; DestDir: "' + $destination + '"; Flags: ignoreversion'
}
$entries += 'Source: "{#PayloadDir}\package-manifest.json"; DestDir: "{app}"; Flags: ignoreversion'
foreach ($relative in @('README.md', 'third_party\README.md', 'third_party\detours\LICENSE.md', 'quicktest\LICENSE', 'LICENSE')) {
    $source = Join-Path $projectRoot $relative
    if (!(Test-Path -LiteralPath $source)) { continue }
    $parent = Split-Path -Parent $relative
    $destination = if ($parent) { '{app}\' + $parent } else { '{app}' }
    $entries += 'Source: "' + $source + '"; DestDir: "' + $destination + '"; Flags: ignoreversion'
}
$include = Join-Path $projectRoot 'build\installer\payload-files.iss'
[IO.File]::WriteAllText($include, ($entries -join "`r`n"), [Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
& $compilerPath ('/DAppVersion=' + $Version) ('/DPayloadDir=' + $payloadRoot) ('/DFileList=' + $include) ('/DProjectRoot=' + $projectRoot) ('/O' + $outputRoot) (Join-Path $PSScriptRoot 'installer\ScriptRoom.iss')
if ($LASTEXITCODE) { throw 'Installer compilation failed' }
$installer = Join-Path $outputRoot 'ScriptRoom-Setup.exe'
if (!(Test-Path -LiteralPath $installer)) { throw 'Installer output missing' }
Write-Output "INSTALLER $installer"
Write-Output "SHA256 $((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant())"
