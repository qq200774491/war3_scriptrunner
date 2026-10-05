"""quicktest 的版本门禁、隔离插件配置和跨进程启动信号。"""
import asyncio
import configparser
import ctypes
from ctypes import wintypes
import hashlib
from pathlib import Path
import socket
import struct
import subprocess

VERIFIED_GAME_SHA256 = 'e04d1716603c075eb0c8e1e21cf1093a664adc5249efab396bfa08d7b09d0c3a'
VERIFIED_TIMESTAMP = 0x56BD0E1C


def verify_game(path):
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        stream.seek(0x3c)
        pe_offset, = struct.unpack('<I', stream.read(4))
        stream.seek(pe_offset)
        header = stream.read(12)
    if digest != VERIFIED_GAME_SHA256 or header[:4] != b'PE\0\0' or struct.unpack_from('<HI', header, 4)[0] != 0x14c:
        raise ValueError('quicktest requires the verified original x86 Game.dll; unknown hash rejected')
    if struct.unpack_from('<I', header, 8)[0] != VERIFIED_TIMESTAMP:
        raise ValueError('quicktest Game.dll timestamp mismatch')


def check_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        try:
            probe.bind(('0.0.0.0', 6112))
        except OSError as error:
            raise RuntimeError('quicktest requires free TCP port 6112; close the previous test game first') from error


def stage_editor(source, destination, dll, copy):
    plugins = destination / 'plugin/warcraft3'
    for path in (source / 'plugin/warcraft3').glob('*.dll'):
        copy(path, plugins / path.name)
    config = configparser.ConfigParser()
    config.read(source / 'plugin/warcraft3/config.cfg', encoding='utf-8-sig')
    config['Enable'][dll.name] = '1'
    copy(dll, plugins / dll.name)
    with (plugins / 'config.cfg').open('w', encoding='utf-8') as stream:
        config.write(stream)
    copy(source / 'bin/EverConfig.cfg', destination / 'bin/EverConfig.cfg')
    return destination.resolve()


def command(game, editor, map_path, room, player, log, ini=None):
    if player not in (0, 1):
        raise ValueError('quicktest currently requires players 0 and 1')
    args = [str(game / 'war3.exe'), '-window', '-opengl', '-kkwe', str(editor)]
    args += ['-host', '2', str(map_path)] if player == 0 else ['-join']
    args += ['--qt-room', room, '--qt-player', f'SRPlayer{player}', '--qt-log', str(log)]
    if ini is not None:
        args += ['--qt-ini', str(ini.resolve())]
    return subprocess.list2cmdline(args)


class HostSignals:
    """只查询本次 room 的命名事件；不在游戏 DLL 加载锁内等待。"""
    def __init__(self, room):
        self.names = {suffix: f'Local\\SharedScriptQuicktest-{room}-{suffix}'
                      for suffix in ('ready', 'failed', 'started')}
        self.handles = {}
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.OpenEventW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
        self.kernel.OpenEventW.restype = wintypes.HANDLE
        self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel.WaitForSingleObject.restype = wintypes.DWORD
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL

    def signaled(self, suffix):
        handle = self.handles.get(suffix)
        if not handle:
            handle = self.kernel.OpenEventW(0x100000, False, self.names[suffix])
            if not handle:
                error = ctypes.get_last_error()
                if error == 2:
                    return False
                raise ctypes.WinError(error)
            self.handles[suffix] = handle
        result = self.kernel.WaitForSingleObject(handle, 0)
        if result == 0xffffffff:
            raise ctypes.WinError(ctypes.get_last_error())
        return result == 0

    def check_failure(self):
        if self.signaled('failed'):
            raise RuntimeError('quicktest host signaled failure; inspect quicktest-0.log')

    @property
    def started(self):
        self.check_failure()
        return self.signaled('started')

    async def wait(self, timeout):
        deadline = asyncio.get_running_loop().time() + timeout
        while asyncio.get_running_loop().time() < deadline:
            self.check_failure()
            if self.signaled('ready'):
                return
            await asyncio.sleep(0.05)
        raise TimeoutError(f'quicktest host was not ready within {timeout}s; inspect quicktest-0.log')

    def close(self):
        for handle in self.handles.values():
            self.kernel.CloseHandle(handle)
        self.handles.clear()
