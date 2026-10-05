"""构建 Win32 Release；合并 Windows 环境变量大小写，避免 MSBuild 的 Path/PATH 冲突。"""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
environment = {key.upper(): value for key, value in os.environ.items()}
environment['Path'] = environment.pop('PATH')
for command in [
    ['cmake', '-S', str(root), '-B', str(root / 'build'), '-G', 'Visual Studio 17 2022', '-A', 'Win32'],
    ['cmake', '--build', str(root / 'build'), '--config', 'Release'],
]:
    subprocess.run(command, env=environment, cwd=root, check=True)
