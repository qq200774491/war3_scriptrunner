$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
python tools/build.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# 测试房间最长保留 15 分钟；Ctrl+C 关闭共享执行器和局域网房主。
python tools/game_room.py --lan --launch --seconds 900
exit $LASTEXITCODE
