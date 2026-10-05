"""用现有项目地图验证原生双端建房/加入，不重新打包地图。"""
import argparse
import asyncio
import json
import math
import os
from pathlib import Path
import sys
import uuid

ROOT = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.game_room import copy_changed, launch_game, prepare_game_directory, validate_editor, quote
from tools import quicktest_backend
from shared_room.server import RoomServer
from tools.cloud_console import CloudConsole
from tools.client_archives import INI_NAME, launch_lock, prepare_snapshots


def install_cloud_entry(cloud, source, game, folder):
    players = []
    for player in cloud.players:
        info = cloud.connection_info(player)
        players.append(f'[{player}]={{port={info["port"]},token={quote(info["token"])}}}')
        (folder / f'mls-player-{player}.log').write_text('', encoding='utf-8')
    profile = (f'{{room={quote(cloud.room_id)},relay=0,sync_chunk=350,sync_immediate=true,logs={quote(folder)},'
        f'dll={quote(ROOT / "build/Release/shared_room_native.dll")},players={{' + ','.join(players) + '}}')
    paths = ';'.join(str(path).replace('\\', '/') for path in
        (ROOT / 'map/?.lua', source.parent / '?.lua', source.parent / '?/init.lua'))
    bootstrap = (f'package.path = {quote(paths)} .. ";" .. package.path\n'
        f'require("legacy_mls").install({profile})\n')
    (game / source.name).write_text(bootstrap + source.read_text(encoding='utf-8'), encoding='utf-8')


def prepare_project_clients(args, source_map):
    base = args.game.resolve()
    if base == args.source_game.resolve():
        raise ValueError('game 必须使用独立测试目录，不能指向 source_game')
    storage = args.archives.resolve() / 'clients' if args.archives else base / 'clients'
    # 先拒绝仍有旧客户端运行的目录，避免开局快照覆盖运行中的数据。
    executables = [base / 'war3.exe'] + [storage / f'player-{player}' / 'war3.exe' for player in (0, 1)]
    for executable in executables:
        if executable.exists():
            try:
                with executable.open('r+b'):
                    pass
            except OSError as error:
                raise RuntimeError(f'请先关闭旧测试客户端，无法准备存档：{executable}') from error
    prepare_game_directory(args.source_game, base)
    # 只有已有云端旧档的项目迁移旧版共用 INI；新项目不继承别的项目存档。
    previous = not args.archives or any(args.archives.glob('slot-*.archive'))
    archives = prepare_snapshots(storage, base / INI_NAME if previous else None)
    relative = Path('Maps/Test/Project.w3x')
    copy_changed(source_map, base / relative)
    if args.local_entry:
        copy_changed(args.local_entry, base / args.local_entry.name)
    return archives, relative


async def main(args):
    with launch_lock(ROOT / 'build/game'):
        await run_project(args)


async def run_project(args):
    if args.cloud_scripts and (not args.local_entry or not args.archives):
        raise ValueError('--cloud-scripts requires --local-entry and an explicit --archives directory')
    if args.cloud_scripts and not (args.cloud_scripts / 'main.lua').is_file():
        raise FileNotFoundError(f'云脚本目录必须直接包含 main.lua：{args.cloud_scripts}')
    source_map = args.map.resolve(strict=True)
    editor = validate_editor(args.editor)
    quicktest_backend.verify_game(args.source_game / 'Game.dll')
    quicktest_backend.check_port()
    folder = ROOT / 'build/game'
    folder.mkdir(parents=True, exist_ok=True)
    if args.local_entry:
        entry = args.local_entry.resolve(strict=True)
    archives, relative = prepare_project_clients(args, source_map)
    game = args.game.resolve()
    copy_changed(editor / 'plugin/warcraft3/yd_loader.dll', folder / 'yd_loader.dll')
    staged = quicktest_backend.stage_editor(editor, folder / 'editor',
        ROOT / 'build/Release/zz_shared_quicktest.dll', copy_changed)
    console = CloudConsole(folder, show=not args.no_console) if args.cloud_scripts else None
    cloud = RoomServer(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll',
        args.cloud_scripts, relay_player=0, archives=args.archives, timeout=20,
        on_log=console.cloud_log) if args.cloud_scripts else None
    room = cloud.room_id if cloud else str(uuid.uuid4())
    signals = quicktest_backend.HostSignals(room)
    environment = {key.upper(): value for key, value in os.environ.items()}
    environment['Path'] = str(editor / 'bin') + ';' + environment.pop('PATH', '')
    configurations = []
    for player in (0, 1):
        log = folder / f'quicktest-{player}.log'
        log.write_text('', encoding='utf-8')
        configurations.append(dict(editor=editor, game=game, files=folder,
            command=quicktest_backend.command(game, staged, relative, room, player, log, ini=archives[player])))
    (folder / 'launch-manifest.json').write_text(json.dumps(configurations, default=str,
        ensure_ascii=False, indent=2), encoding='utf-8')
    result = dict(room=room, source_map=str(source_map), game_pids=[], phase='starting',
        cloud_state_verified=False,
        client_games={str(player): str(game) for player in archives},
        client_archives={str(player): str(path) for player, path in archives.items()})
    if args.local_entry:
        result['local_entry'] = str(entry)
    print(f'PROJECT_MAP {source_map}; room {room}', flush=True)
    if console:
        console.write('房间', room)
        console.write('地图', source_map)
        console.write('云脚本目录', args.cloud_scripts.resolve())
        console.write('测试存档', args.archives.resolve())
        for player, path in archives.items():
            console.write(f'玩家 {player} 客户端存档', path)
        console.write('共用魔兽', game / 'war3.exe')
        console.write('读档', '各玩家独立保存；开局已按玩家编号汇总读取快照。')
        console.write('服务', f'本轮最多运行 {args.seconds} 秒。到期将关闭云端服务；游戏窗口需自行退出。')
        result['console_log'] = str(console.path)
        print(f'CLOUD_CONSOLE {console.path}', flush=True)
    try:
        if cloud:
            await cloud.start()
            console.write('服务', '已监听本机连接，等待两名玩家入图后启动唯一云脚本宿主。')
            install_cloud_entry(cloud, entry, game, folder)
            result['cloud_scripts'] = str(args.cloud_scripts.resolve())
            result['archives'] = str(args.archives.resolve())
        for index, config in enumerate(configurations):
            if index == 1:
                await signals.wait(args.startup_timeout)
            result['game_pids'].append(await launch_game(ROOT / 'build/Release/lua_probe.exe',
                editor / 'bin/luacore.dll', config, folder, environment))
        result['phase'] = 'waiting_for_lan'
        previous = ['', '']
        for _ in range(args.seconds):
            await asyncio.sleep(1)
            if cloud and cloud.failed:
                raise RuntimeError('shared cloud room failed: ' + cloud.failed)
            signals.check_failure()
            for player in (0, 1):
                current = (folder / f'quicktest-{player}.log').read_text(encoding='utf-8', errors='replace')
                if current != previous[player]:
                    print(current[len(previous[player]):], end='', flush=True)
                    previous[player] = current
            if signals.started:
                result['phase'] = 'lan_game_started'
            if result['phase'] != 'lan_game_started' and _ >= args.startup_timeout:
                raise TimeoutError('native LAN game did not start before deadline')
            if cloud and cloud.active and _ % 10 == 0:
                print(f'CLOUD_ACTIVE clients={len(cloud.sessions)} replies={cloud.delivery}', flush=True)
                console.write('运行', f'在线 {len(cloud.sessions)}/2；云端回包 {cloud.delivery}；'
                              f'剩余约 {args.seconds - _} 秒')
            elif console and _ % 10 == 0:
                console.write('等待', f'已连接 {len(cloud.sessions)}/2')
    except BaseException as error:
        result['error'] = str(error)
        if console:
            console.write('已停止' if isinstance(error, (KeyboardInterrupt, asyncio.CancelledError)) else '故障',
                          str(error) or '用户停止了启动工具')
        raise
    finally:
        signals.close()
        if cloud:
            result['cloud_callbacks'] = cloud.delivery
            result['cloud_failure'] = cloud.failed
            result['cloud_logs'] = list(cloud.host.logs)
            await cloud.close()
        if console:
            console.close()
        (folder / 'project-result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, help='JSON configuration; explicit command-line options override it')
    parser.add_argument('--map', type=Path)
    parser.add_argument('--local-entry', type=Path, help='Existing local Lua entry to stage in the game working directory')
    parser.add_argument('--cloud-scripts', type=Path, help='Explicit shared cloud script directory')
    parser.add_argument('--archives', type=Path, help='Isolated test archive directory, keyed by fixed player slot')
    parser.add_argument('--editor', type=Path)
    parser.add_argument('--source-game', type=Path)
    parser.add_argument('--game', type=Path)
    parser.add_argument('--seconds', type=int)
    parser.add_argument('--startup-timeout', type=float)
    parser.add_argument('--no-console', action='store_true', help='Keep cloud logs on disk without opening a viewer window')
    args = parser.parse_args(argv)
    defaults = dict(map=None, local_entry=None, cloud_scripts=None, archives=None,
                    editor=Path('D:/war3Project/KKWE插件'), source_game=Path('D:/war3Project/war3'),
                    game=ROOT / 'build/warcraft-demo', seconds=180, startup_timeout=90)
    paths = {'map', 'local_entry', 'cloud_scripts', 'archives', 'editor', 'source_game', 'game'}
    if args.config:
        config = args.config.resolve(strict=True)
        settings = json.loads(config.read_text(encoding='utf-8-sig'))
        if not isinstance(settings, dict) or set(settings) - defaults.keys():
            parser.error('configuration must be an object containing only supported field names')
        for name, value in settings.items():
            if name in paths:
                if not isinstance(value, str) or not value.strip():
                    parser.error(f'{name} must be a nonempty path string')
                path = Path(value)
                defaults[name] = path if path.is_absolute() else config.parent / path
            else:
                if type(value) not in (int, float) or not math.isfinite(value) or value <= 0 or (name == 'seconds' and type(value) is not int):
                    parser.error(f'{name} must be a positive number; seconds must be an integer')
                defaults[name] = value
    for name, value in defaults.items():
        if getattr(args, name) is None:
            setattr(args, name, value)
    if args.map is None:
        parser.error('--map or configuration field map is required')
    if args.seconds <= 0 or args.startup_timeout <= 0:
        parser.error('seconds and startup_timeout must be positive')
    return args


if __name__ == '__main__':
    asyncio.run(main(parse_args()))
