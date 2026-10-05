"""构建无需 Python 的 Windows 便携启动器；用户配置和存档不会被覆盖。"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.project_quicktest import parse_args

NATIVE_FILES = ('script_host.exe', 'lua_probe.exe', 'shared_room_native.dll', 'zz_shared_quicktest.dll')
PATH_FIELDS = ('map', 'local_entry', 'cloud_scripts', 'archives', 'editor', 'source_game', 'game')


def export_config(source, destination):
    # 升级便携工具时保留用户改过的配置；首次导出沿用源配置的存档位置。
    if destination.exists():
        return
    args = parse_args(['--config', str(source)])
    settings = {name: str(getattr(args, name).resolve()) for name in PATH_FIELDS if getattr(args, name)}
    settings.update(seconds=args.seconds, startup_timeout=args.startup_timeout)
    destination.write_text(json.dumps(settings, ensure_ascii=False, indent=2), encoding='utf-8')


def build(output, config, skip_native=False):
    if not skip_native:
        subprocess.run([sys.executable, str(ROOT / 'tools/build.py')], cwd=ROOT, check=True)
    for name in NATIVE_FILES:
        if not (ROOT / 'build/Release' / name).is_file():
            raise FileNotFoundError(f'请先编译原生组件：{name}')
    # PyInstaller 每次构建使用独立目录，避免重建时删除用户放进发行目录的配置或存档。
    stage = ROOT / 'build/packaging' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    stage.mkdir(parents=True)
    spec = stage / 'ScriptRoom.spec'
    spec.write_text(f'''
a = Analysis([{str(ROOT / 'tools/launcher.py')!r}], pathex=[{str(ROOT)!r}],
             binaries=[], datas=[], hiddenimports=[], hookspath=[], runtime_hooks=[], excludes=[])
p = PYZ(a.pure)
cli = EXE(p, a.scripts, [], exclude_binaries=True, name='ScriptRoomCLI', console=True, upx=False)
gui = EXE(p, a.scripts, [], exclude_binaries=True, name='ScriptRoom', console=False, upx=False)
collection = COLLECT(cli, gui, a.binaries, a.datas, strip=False, upx=False, name='ScriptRoom')
''', encoding='utf-8')
    subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--distpath', str(stage / 'dist'),
                    '--workpath', str(stage / 'work'), str(spec)], cwd=ROOT, check=True)
    package = stage / 'dist/ScriptRoom'
    shutil.copytree(package, output, dirs_exist_ok=True)
    assets = [ROOT / 'scriptrunner.dll', ROOT / 'tools/launch_game.lua', ROOT / 'GUI与命令行启动.md']
    assets += list((ROOT / 'map').glob('*.lua'))
    assets += [ROOT / 'build/Release' / name for name in NATIVE_FILES]
    for source in assets:
        target = output / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    export_config(config, output / '本地测试配置.json')
    shipped = [path.relative_to(package) for path in package.rglob('*') if path.is_file()]
    shipped += [path.relative_to(ROOT) for path in assets]
    manifest = {str(path).replace('\\', '/'): hashlib.sha256((output / path).read_bytes()).hexdigest()
                for path in sorted(shipped)}
    (output / 'package-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'PACKAGE {output}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/package/ScriptRoom')
    parser.add_argument('--config', type=Path, default=ROOT / '本地测试配置.json')
    parser.add_argument('--skip-native', action='store_true', help='只供确认原生 Release 已编译时使用')
    args = parser.parse_args()
    build(args.output.resolve(), args.config.resolve(strict=True), args.skip_native)


if __name__ == '__main__':
    main()
