param(
    [string]$Config = (Join-Path $PSScriptRoot '本地测试配置.json'),
    [int]$Seconds = 0,
    [switch]$NoConsole
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
python tools/build.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# 复用现有地图与其本地脚本加载机制；不打包、不结束其他魔兽进程。
# 两端共用原版执行器，本地测试档按固定玩家槽位分别保存。
$launchArgs = @('tools/project_quicktest.py', '--config', $Config)
if ($Seconds -gt 0) { $launchArgs += @('--seconds', [string]$Seconds) }
if ($NoConsole) { $launchArgs += '--no-console' }
python @launchArgs
exit $LASTEXITCODE
