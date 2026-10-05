"""独立云脚本日志窗口；关闭查看窗口不会关闭房间服务。"""
import argparse
import codecs
import ctypes
from datetime import datetime
from pathlib import Path
import subprocess
import sys
import time
import uuid


class CloudConsole:
    def __init__(self, folder, show=True):
        folder = Path(folder).resolve()
        folder.mkdir(parents=True, exist_ok=True)
        self.path = folder / f'cloud-{datetime.now():%Y%m%d-%H%M%S}-{uuid.uuid4().hex[:8]}.log'
        self.file = self.path.open('w', encoding='utf-8', buffering=1)
        self.viewer = None
        (folder / 'cloud-console-latest.txt').write_text(str(self.path), encoding='utf-8')
        self.write('控制台', '实时云脚本日志。关闭此窗口只关闭查看器，不会结束游戏或房间。')
        if show:
            try:
                command = ([sys.executable, '--cloud-console'] if getattr(sys, 'frozen', False)
                           else [sys.executable, '-u', str(Path(__file__).resolve())])
                self.viewer = subprocess.Popen(command + ['--view', str(self.path)],
                                                creationflags=subprocess.CREATE_NEW_CONSOLE)
            except OSError as error:
                self.write('控制台', f'无法打开独立窗口：{error}；日志仍保存到 {self.path}')

    def write(self, source, message):
        for line in str(message).splitlines() or ['']:
            self.file.write(f'[{datetime.now():%H:%M:%S}] [{source}] {line}\n')

    def cloud_log(self, entry):
        level = {0: 'DEBUG', 1: 'INFO', 2: 'WARN', 3: 'ERROR', 4: 'FATAL'}.get(entry['level'], str(entry['level']))
        self.write(f'云脚本/{level}', entry['message'])

    def close(self):
        if self.file.closed:
            return
        self.write('控制台', '本轮房间服务已结束。此窗口保留历史日志；继续测试请重新启动双人测试。')
        self.file.close()
        self.path.with_suffix('.done').touch()


def follow(path):
    # 二进制增量解码，防止查看器恰好读到一个 UTF-8 字符的半截。
    decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')
    pending = ''
    with path.open('rb') as source:
        while True:
            chunk = source.read(65536)
            if chunk:
                pending += decoder.decode(chunk)
                lines = pending.split('\n')
                pending = lines.pop()
                for line in lines:
                    yield line
            elif path.with_suffix('.done').exists():
                # 结束标记写入前日志已关闭，再读一次以消除 EOF 与标记之间的竞态。
                pending += decoder.decode(source.read(), final=True)
                if pending:
                    yield pending.rstrip('\n')
                return
            else:
                time.sleep(.1)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--view', type=Path)
    group.add_argument('--latest', type=Path)
    args = parser.parse_args(argv)
    path = args.view
    if args.latest:
        path = Path((args.latest / 'cloud-console-latest.txt').read_text(encoding='utf-8'))
    ctypes.windll.kernel32.SetConsoleTitleW('map · 共享云脚本控制台')
    print(f'日志文件：{path}\n', flush=True)
    try:
        for line in follow(path):
            print(line, flush=True)
    except KeyboardInterrupt:
        return
    input('\n本轮已结束，按回车关闭查看器……')


if __name__ == '__main__':
    try:
        main()
    except OSError as error:
        print(f'无法读取云脚本日志：{error}', file=sys.stderr)
        raise SystemExit(1)
