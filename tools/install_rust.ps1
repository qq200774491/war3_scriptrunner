param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\ScriptRoom'),
    [string]$ShortcutDirectory = (Join-Path ([Environment]::GetFolderPath('Programs')) 'ScriptRoom'),
    [string]$Config,
    [switch]$SkipNative,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$installRoot = [IO.Path]::GetFullPath($InstallDir)

# 更新前要求退出该安装目录中的程序，避免覆盖运行中的组件。
$running = @(Get-Process | Where-Object {
    $_.Path -and $_.Path.StartsWith($installRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
})
if ($running.Count) {
    throw "Please close ScriptRoom and its running services before installing: $($running.Name -join ', ')"
}

# 复用发行流程，只覆盖程序资源；已有配置、主题和存档由打包脚本保留。
& (Join-Path $PSScriptRoot 'package_rust.ps1') -Output $installRoot -Config $Config -SkipNative:$SkipNative -SkipBuild:$SkipBuild

New-Item -ItemType Directory -Path $ShortcutDirectory -Force | Out-Null
$shell = New-Object -ComObject WScript.Shell
try {
    $shortcut = $shell.CreateShortcut((Join-Path ([IO.Path]::GetFullPath($ShortcutDirectory)) 'ScriptRoom.lnk'))
    try {
        $shortcut.TargetPath = Join-Path $installRoot 'ScriptRoom.exe'
        $shortcut.WorkingDirectory = $installRoot
        $shortcut.IconLocation = (Join-Path $installRoot 'ScriptRoom.exe') + ',0'
        $shortcut.Description = 'ScriptRoom'
        $shortcut.Save()
    }
    finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
}
finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }

Write-Output "INSTALLED $installRoot"
Write-Output "SHORTCUT $(Join-Path $ShortcutDirectory 'ScriptRoom.lnk')"
