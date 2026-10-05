"""共享云脚本双人测试：GUI 与命令行共用同一启动后端。"""
import argparse
import asyncio
import ctypes
import json
from pathlib import Path
import sys

ROOT = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def check_configuration(args):
    from tools.game_room import validate_editor
    from tools.quicktest_backend import verify_game
    validate_editor(args.editor)
    verify_game(args.source_game / 'Game.dll')
    required = [args.map, ROOT / 'scriptrunner.dll', ROOT / 'tools/launch_game.lua',
                ROOT / 'map/legacy_mls.lua']
    required += [ROOT / 'build/Release' / name for name in
                 ('script_host.exe', 'lua_probe.exe', 'shared_room_native.dll', 'zz_shared_quicktest.dll')]
    if args.local_entry:
        required.append(args.local_entry)
    if args.cloud_scripts:
        if not args.local_entry or not args.archives:
            raise ValueError('云脚本测试必须设置本地 Lua 入口和存档目录')
        required.append(args.cloud_scripts / 'main.lua')
    for path in required:
        if not path.is_file():
            raise FileNotFoundError(f'找不到所需文件：{path}')
    if args.game.resolve() == args.source_game.resolve():
        raise ValueError('测试魔兽目录不能与原始魔兽目录相同')
    return {'ok': True, 'map': str(args.map.resolve()), 'game': str(args.game.resolve()),
            'cloud_scripts': str(args.cloud_scripts.resolve()) if args.cloud_scripts else None,
            'archives': str(args.archives.resolve()) if args.archives else None,
            'players': 2}


async def supervise(operation, stop_file):
    """停止请求取消整个启动操作，并等待后端 finally 完成云端清理。"""
    task = asyncio.create_task(operation)
    async def watch_stop():
        while not stop_file.exists():
            await asyncio.sleep(.2)
    watcher = asyncio.create_task(watch_stop())
    try:
        await asyncio.wait((task, watcher), return_when=asyncio.FIRST_COMPLETED)
        if not task.done():
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
            print('STOPPED 云服务已停止；游戏窗口请正常退出。', flush=True)
        else:
            await task
    finally:
        watcher.cancel()
        if not task.done():
            task.cancel()
        await asyncio.gather(task, watcher, return_exceptions=True)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv[:1] == ['--cloud-console']:
        from tools.cloud_console import main as console_main
        console_main(argv[1:])
        return 0
    if argv[:1] == ['--gui'] or (not argv and Path(sys.executable).stem != 'ScriptRoomCLI'):
        from tools.launcher_gui import main as gui_main
        gui_main(argv[1:] if argv else [])
        return 0
    if argv[:1] == ['--cli']:
        argv.pop(0)
    control = argparse.ArgumentParser(add_help=False)
    control.add_argument('--check', action='store_true')
    control.add_argument('--stop-file', type=Path)
    options, remaining = control.parse_known_args(argv)
    if '--help' in remaining or '-h' in remaining:
        print('ScriptRoomCLI [--config 配置.json] [--check] [--no-console] [--seconds 秒数]\n'
              '--check 只检查配置，不启动游戏；--gui 打开配置界面。\n')
    if not any(value == '--config' or value.startswith('--config=') for value in remaining):
        if (ROOT / '本地测试配置.json').is_file():
            remaining = ['--config', str(ROOT / '本地测试配置.json')] + remaining
    from tools.project_quicktest import main as launch, parse_args
    args = parse_args(remaining)
    if options.check:
        print(json.dumps(check_configuration(args), ensure_ascii=False, indent=2), flush=True)
        return 0
    check_configuration(args)
    if options.stop_file:
        asyncio.run(supervise(launch(args), options.stop_file))
    else:
        asyncio.run(launch(args))
    return 0


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        if stream is not None:
            stream.reconfigure(encoding='utf-8', errors='replace')
    # PyInstaller 的运行库搜索目录不能传给编辑器的 32 位原生子进程。
    if getattr(sys, 'frozen', False) and (Path(sys.executable).stem == 'ScriptRoomCLI' or '--cli' in sys.argv):
        ctypes.windll.kernel32.SetDllDirectoryW(None)
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print('已停止。', file=sys.stderr)
        raise SystemExit(130)
    except Exception as error:
        if sys.stderr is not None:
            print(f'启动失败：{error}', file=sys.stderr, flush=True)
        else:
            from tkinter import messagebox
            messagebox.showerror('启动失败', str(error))
        raise SystemExit(1)
