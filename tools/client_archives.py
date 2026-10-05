"""每个固定测试玩家拥有独立 INI；平台读写重定向，游戏程序共用。"""
import configparser
import contextlib
import ctypes
from ctypes import wintypes
import io
import json
import msvcrt
import os
from pathlib import Path
import re
import tempfile

PLAYERS = (0, 1)
INI_NAME = 'dz_w3_plugin.ini'
ARCHIVE_KEY = re.compile(r'^SSV-(\d+)-.+$', re.IGNORECASE)


@contextlib.contextmanager
def launch_lock(folder):
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / '.project-launch.lock').open('a+b') as lock:
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise RuntimeError('已有项目测试启动器在运行，请先结束上一轮测试') from error
        try:
            yield
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            pending = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(pending, path)
    finally:
        if pending is not None:
            pending.unlink(missing_ok=True)


def read_ini(data):
    # Windows Profile API 支持 UTF-16 BOM；无 BOM 文件按系统代码页处理。
    encoding = 'utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'mbcs'
    if data.startswith(b'\xef\xbb\xbf'):
        encoding = 'utf-8-sig'
    config = configparser.ConfigParser(interpolation=None, strict=True, delimiters=('=',),
                                       comment_prefixes=(';',), empty_lines_in_values=False)
    config.optionxform = str
    config.read_string(data.decode(encoding))
    if config.defaults():
        raise ValueError('存档 INI 不支持 DEFAULT 继承，避免与原插件读取结果不同')
    sections = {name.lower(): name for name in config.sections()}
    if len(sections) != len(config.sections()):
        raise ValueError('存档 INI 存在仅大小写不同的重复节')
    for section in config.values():
        names = [key.lower() for key in section]
        if len(set(names)) != len(names) or any('\n' in value for value in section.values()):
            raise ValueError('存档 INI 包含重复键或多行值，不能安全迁移')
    name = sections.get('dzapi', 'DzAPI')
    if name not in config:
        config.add_section(name)
    return config, name


def encode_ini(config):
    output = io.StringIO()
    config.write(output, space_around_delimiters=False)
    return output.getvalue().encode('utf-16')


@contextlib.contextmanager
def snapshot_locks(paths):
    # 与游戏插件的 CreateFileW(share=0) 锁互斥；启动器退出也不允许覆盖仍在运行的客户端。
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handles = []
    try:
        for path in paths:
            path.parent.mkdir(parents=True, exist_ok=True)
            handle = kernel.CreateFileW(str(path) + '.lock', 0xC0000000, 0, None, 4, 0x80, None)
            if handle == wintypes.HANDLE(-1).value:
                raise RuntimeError(f'客户端存档仍被占用或不可写，请先关闭旧客户端：{path}')
            handles.append(handle)
        yield
    finally:
        for handle in handles:
            kernel.CloseHandle(handle)


def prepare_snapshots(folder, legacy_ini=None):
    folder = Path(folder).resolve()
    paths = {player: folder / f'player-{player}.ini' for player in PLAYERS}
    with snapshot_locks(paths.values()):
        return _prepare_snapshots(folder, paths, legacy_ini)


def _prepare_snapshots(folder, paths, legacy_ini):
    """先读取所有权威槽位，再更新各端快照；不从另一端的旧快照回填自己的档。"""
    marker = folder / 'layout.json'
    layout = json.loads(marker.read_text(encoding='utf-8')) if marker.exists() else None
    if layout not in (None, {'version': 1, 'players': list(PLAYERS)}, {'version': 2, 'players': list(PLAYERS)}):
        raise ValueError('未知客户端存档布局，停止启动以保留原文件')
    migrating = layout is not None and layout['version'] == 1
    legacy = Path(legacy_ini) if legacy_ini else None
    backup = folder / 'legacy-dz_w3_plugin.ini.bak'
    seed = b'[DzAPI]\r\n'
    if layout is None:
        if backup.exists():
            seed = backup.read_bytes()
        elif legacy and legacy.is_file():
            seed = legacy.read_bytes()
            read_ini(seed)
            atomic_write(backup, seed)
    documents = {}
    values = {}
    for player, path in paths.items():
        source = folder / f'player-{player}' / INI_NAME if migrating else path
        # 完成迁移后丢失文件是异常，不用旧共用档或队友副本复活。
        data = source.read_bytes() if layout is not None or source.exists() else seed
        config, section = read_ini(data)
        documents[player] = config, section
        for key, value in config[section].items():
            match = ARCHIVE_KEY.fullmatch(key)
            if match and int(match[1]) == player:
                values[key] = value
    if migrating:
        for player in PLAYERS:
            old = folder / f'player-{player}' / INI_NAME
            preserved = folder / 'backups' / f'v1-player-{player}.ini'
            if not preserved.exists():
                atomic_write(preserved, old.read_bytes())
    for player, path in paths.items():
        config, section = documents[player]
        for key in list(config[section]):
            match = ARCHIVE_KEY.fullmatch(key)
            if match and int(match[1]) in PLAYERS:
                del config[section][key]
        config[section].update(values)
        atomic_write(path, encode_ini(config))
    atomic_write(marker, json.dumps({'version': 2, 'players': list(PLAYERS)}).encode('utf-8'))
    return paths
