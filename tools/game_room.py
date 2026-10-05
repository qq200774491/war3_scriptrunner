"""准备独立的双人测试地图并维持共享脚本房间；可选沿用编辑器入口启动游戏。"""
import argparse
import asyncio
import configparser
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from shared_room.server import RoomServer
from tools import quicktest_backend


def quote(value):
    return json.dumps(str(value), ensure_ascii=False)


def lua_table(values):
    return 'return {' + ','.join(f'{key}={quote(value)}' for key, value in values.items()) + '}\n'


def copy_changed(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        with source.open('rb') as left, destination.open('rb') as right:
            if hashlib.file_digest(left, 'sha256').digest() == hashlib.file_digest(right, 'sha256').digest():
                return
    shutil.copyfile(source, destination)


def prepare_game_directory(source, destination):
    # 测试版仅复制运行所需文件，不带原游戏目录里的额外插件和配置。
    for name in ('War3.exe', 'Game.dll', 'Storm.dll', 'Mss32.dll', 'ijl15.dll', 'msvcr120.dll',
                 'msvcp120.dll', 'war3.mpq', 'War3x.mpq', 'War3xLocal.mpq', 'War3Patch.mpq'):
        copy_changed(source / name, destination / name)


def validate_editor(editor):
    editor = editor.resolve()
    plugins = editor / 'plugin/warcraft3'
    config_path = plugins / 'config.cfg'
    if not config_path.is_file():
        raise ValueError(f'Editor root must contain plugin/warcraft3/config.cfg: {editor}')
    config = configparser.ConfigParser()
    config.read(config_path, encoding='utf-8-sig')
    for name in ('yd_mutil_warcraft3.dll', 'yd_lua_engine.dll', 'kkapi.dll', 'kkapi_local_plugin.dll'):
        if config.get('Enable', name, fallback='0').strip() != '1' or not (plugins / name).is_file():
            raise ValueError(f'Required game plugin is missing or disabled: {name}')
    return editor


def game_command(game, editor, destination, lan, auto_join=False):
    # loader 将 -kkwe 原样作为插件根目录，不能传 editor.parent。
    arguments = [str(game / 'war3.exe'), '-window', '-opengl', '-kkwe', str(editor)]
    if not lan:
        arguments += ['-loadfile', str(destination)]
    elif auto_join:
        arguments += ['-auto']
    return subprocess.list2cmdline(arguments)


async def launch_game(probe, core, config, folder, environment):
    (folder / 'launch_config.lua').write_text(lua_table(config), encoding='utf-8')
    process = await asyncio.create_subprocess_exec(str(probe), str(core), str(ROOT / 'tools/launch_game.lua'),
        cwd=ROOT, env=environment, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        stdout, stderr = await asyncio.wait_for(process.communicate(), 15)
    except BaseException:
        if process.returncode is None:
            process.kill()
        await process.communicate()
        raise
    if process.returncode:
        raise RuntimeError(stderr.decode('utf-8', errors='replace'))
    output = stdout.decode('utf-8', errors='replace')
    print(output, end='', flush=True)
    for line in output.splitlines():
        if line.startswith('GAME_PID '):
            return int(line[9:])
    raise RuntimeError('game launcher did not return a process id')


async def main(args):
    args.editor = validate_editor(args.editor)
    use_quicktest = args.backend == 'quicktest'
    if use_quicktest:
        if args.auto_join:
            raise ValueError('--auto-join belongs to ydhost; quicktest already manages host/join')
        args.lan = True
        quicktest_backend.verify_game(args.source_game / 'Game.dll')
        if args.launch:
            quicktest_backend.check_port()
    folder = ROOT / 'build/game'
    folder.mkdir(parents=True, exist_ok=True)
    prepare_game_directory(args.source_game, args.game)
    room = RoomServer(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll',
                      ROOT / 'tests/scripts/counter', relay_player=0)
    await room.start()
    lan_host = None
    lan_log = None
    signals = quicktest_backend.HostSignals(room.room_id) if use_quicktest else None
    game_pids = []
    outcome = {'backend': args.backend, 'room': room.room_id, 'game_pids': game_pids, 'result': 'not_started'}
    try:
        source = args.editor / 'example(演示地图)/JAPI/Lua-计时器-圣光.w3x'
        destination = folder / 'shared-room-test.w3x'
        shutil.copyfile(source, destination)
        copy_changed(args.editor / 'plugin/warcraft3/yd_loader.dll', folder / 'yd_loader.dll')
        for name in ('shared_room.lua', 'room_json.lua', 'room_transport.lua'):
            shutil.copyfile(ROOT / 'map' / name, folder / name)
        shutil.copyfile(ROOT / 'map/demo_main.lua', folder / 'main.lua')
        profiles = []
        for player in room.players:
            info = room.connection_info(player)
            profiles.append(f'[{player}]={{port={info["port"]},token={quote(info["token"])}}}')
        profile = (f'return {{room={quote(room.room_id)},relay=0,logs={quote(folder)},'
                   f'dll={quote(ROOT / "build/Release/shared_room_native.dll")},players={{' + ','.join(profiles) + '}}\n')
        (folder / 'room_profile.lua').write_text(profile, encoding='utf-8')
        command = game_command(args.game, args.editor, destination, args.lan, args.auto_join)
        (folder / 'prepare_config.lua').write_text(lua_table(dict(editor=args.editor, game=args.game,
            map=destination, files=folder, command=command)), encoding='utf-8')
        probe = ROOT / 'build/Release/lua_probe.exe'
        core = args.editor / 'bin/luacore.dll'
        environment = {key.upper(): value for key, value in os.environ.items()}
        environment['Path'] = str(args.editor / 'bin') + ';' + environment.pop('PATH', '')
        subprocess.run([str(probe), str(core), str(ROOT / 'tools/prepare_game.lua')], cwd=ROOT, env=environment, check=True)
        relative = Path('Maps/Test/SharedRoom.w3x')
        if args.lan:
            copy_changed(destination, folder / relative)
            copy_changed(destination, args.game / relative)
        if args.lan and not use_quicktest:
            copy_changed(args.editor / 'plugin/ydhost/ydhost.exe', folder / 'ydhost.exe')
            for name in ('msvcp140.dll', 'vcruntime140.dll'):
                copy_changed(args.editor / 'bin' / name, folder / name)
            (folder / 'ydhost.cfg').write_text(
                f'lan_war3version = {args.war3_version}\nbot_defaultgamename = SharedRoomTest\nbot_autostart = 2\n'
                'bot_mappath = Maps\\Test\\SharedRoom.w3x\nbot_mapcfgpath = map.cfg\n', encoding='ascii')
            lan_log = (folder / 'ydhost.log').open('wb')
            lan_host = await asyncio.create_subprocess_exec(str(folder / 'ydhost.exe'), cwd=folder,
                stdout=lan_log, stderr=asyncio.subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        print(f'Room {room.room_id}; map {destination}', flush=True)
        if args.lan and not args.auto_join and not use_quicktest:
            print('In each Warcraft window, open Local Area Network, enter distinct names '
                  'SRPlayer0 / SRPlayer1, and join SharedRoomTest.', flush=True)
        configurations = []
        if use_quicktest:
            quicktest_backend.verify_game(args.game / 'Game.dll')
            staged = quicktest_backend.stage_editor(args.editor, folder / 'editor',
                ROOT / 'build/Release/zz_shared_quicktest.dll', copy_changed)
            for player in (0, 1):
                log = folder / f'quicktest-{player}.log'
                log.write_text('', encoding='utf-8')
                configurations.append(dict(editor=args.editor, game=args.game, files=folder,
                    command=quicktest_backend.command(args.game, staged, relative, room.room_id, player, log)))
            print('QUICKTEST: first game hosts; second joins after HOST_READY.', flush=True)
        else:
            configurations = [dict(editor=args.editor, game=args.game, files=folder, command=command)] * (2 if args.lan else 1)
        (folder / 'launch-manifest.json').write_text(json.dumps(configurations, default=str, ensure_ascii=False, indent=2), encoding='utf-8')
        # 先清理本次专属验收日志，避免上次 PASS 被误认作当前结果。
        for player in room.players:
            (folder / f'player-{player}.log').write_text('', encoding='utf-8')
        if args.launch:
            for index, config in enumerate(configurations):
                if use_quicktest and index == 1:
                    await signals.wait(args.startup_timeout)
                game_pids.append(await launch_game(probe, core, config, folder, environment))
        outcome['result'] = 'waiting_for_game'
        started = asyncio.get_running_loop().time()
        for _ in range(args.seconds):
            await asyncio.sleep(1)
            if room.failed:
                print('ROOM_FAILED ' + room.failed, flush=True)
                outcome['result'] = 'room_failed'
                break
            if lan_host and lan_host.returncode is not None:
                raise RuntimeError(f'LAN host exited with {lan_host.returncode}; inspect build/game/ydhost.log')
            if room.active and _ % 10 == 0:
                print(f'ACTIVE clients={len(room.sessions)} callbacks={room.delivery}', flush=True)
            if args.launch and use_quicktest and not room.active and asyncio.get_running_loop().time() - started > args.startup_timeout:
                raise TimeoutError('both game clients did not join the cloud room; inspect quicktest and player logs')
            passed = all('PASS shared_counter=12 gold=12' in (folder / f'player-{p}.log').read_text(encoding='utf-8', errors='replace') for p in room.players)
            if passed and outcome['result'] != 'counter_pass':
                outcome['result'] = 'counter_pass'
                print('COUNTER_PASS both game logs agree; keep playing to check lockstep stability.', flush=True)
    except BaseException as error:
        outcome['result'] = 'interrupted' if isinstance(error, (KeyboardInterrupt, asyncio.CancelledError)) else 'failed'
        outcome['error'] = str(error)
        raise
    finally:
        outcome.update(callbacks=room.delivery, failure=room.failed, logs=list(room.host.logs))
        (folder / 'host-result.json').write_text(json.dumps(outcome, ensure_ascii=False, indent=2), encoding='utf-8')
        if signals:
            signals.close()
        await room.close()
        if lan_host and lan_host.returncode is None:
            lan_host.terminate()
            await lan_host.wait()
        if lan_log:
            lan_log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--editor', type=Path, default=Path('D:/war3Project/KKWE插件'))
    parser.add_argument('--source-game', type=Path, default=Path('D:/war3Project/war3'))
    parser.add_argument('--game', type=Path, default=ROOT / 'build/warcraft-demo')
    parser.add_argument('--launch', action='store_true')
    parser.add_argument('--backend', choices=('ydhost', 'quicktest'), default='ydhost')
    parser.add_argument('--startup-timeout', type=float, default=45)
    parser.add_argument('--lan', action='store_true')
    parser.add_argument('--auto-join', action='store_true', help='Opt-in loader auto-join; requires a valid local player name')
    parser.add_argument('--war3-version', type=int, default=27, help='LAN minor version; current source game is 1.27')
    parser.add_argument('--seconds', type=int, default=180)
    asyncio.run(main(parser.parse_args()))
