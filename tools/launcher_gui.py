"""地图路径配置、双人启动和实时云端日志界面。"""
import argparse
import codecs
import ctypes
import json
import math
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox
from tools.launcher_view import WorkspaceView
from tools.launcher_theme import THEMES, load_theme, save_theme
import uuid

from tools.client_archives import atomic_write
from tools.launcher import ROOT
from tools.project_quicktest import parse_args

FIELDS = (
    ('map', '地图文件', 'file'),
    ('local_entry', '本地 Lua 入口', 'file'),
    ('cloud_scripts', '云脚本目录', 'folder'),
    ('archives', '测试存档目录', 'folder'),
    ('editor', 'KKWE 编辑器', 'folder'),
    ('source_game', '原始魔兽目录', 'folder'),
    ('game', '共用测试魔兽目录', 'folder'),
)


def worker_command():
    if getattr(sys, 'frozen', False):
        return [str(ROOT / 'ScriptRoomCLI.exe')]
    return [sys.executable, '-u', str(ROOT / 'tools/launcher.py'), '--cli']


class LauncherWindow:
    def __init__(self, window, config, appearance_path=None):
        self.window = window
        self.config_path = Path(config).resolve()
        self.values = {name: tk.StringVar() for name, _, _ in FIELDS}
        self.values.update(seconds=tk.StringVar(value='3600'), startup_timeout=tk.StringVar(value='90'))
        self.status = tk.StringVar(value='就绪：选择配置，检查后启动。')
        self.location = tk.StringVar(value=str(self.config_path))
        self.separate_console = tk.BooleanVar(value=False)
        self.messages = queue.Queue()
        self.process = None
        self.stop_file = None
        self.closing = False
        self.cloud_file = None
        self.cloud_path = None
        self.decoder = None
        self.appearance_path = Path(appearance_path) if appearance_path else ROOT / '界面设置.json'
        self.theme = load_theme(self.appearance_path)
        self.theme_label = tk.StringVar(value=THEMES[self.theme])
        self.view = WorkspaceView(self, FIELDS)
        self.client_pids = set()
        self.checking = False
        self.append('准备就绪。检查配置后，即可启动双人测试。\n')
        if self.config_path.is_file():
            try:
                self.load_config(self.config_path)
            except (Exception, SystemExit) as error:
                self.append(f'配置无法读取：{error}；请打开其他配置或重新填写。\n')
                self.status.set('配置无法读取，请重新填写。')
        window.protocol('WM_DELETE_WINDOW', self.close)
        self.poll_job = window.after(150, self.poll)
        window.bind('<Destroy>', self.on_destroy, add='+')

    def on_destroy(self, event):
        if event.widget is self.window:
            self.window.after_cancel(self.poll_job)
            self.view.dispose()

    def change_theme(self, _event=None):
        chosen = next(key for key, text in THEMES.items() if text == self.theme_label.get())
        if chosen == self.theme:
            return
        try:
            save_theme(self.appearance_path, chosen)
        except OSError as error:
            self.theme_label.set(THEMES[self.theme])
            messagebox.showerror('主题无法保存', str(error))
            return
        # 只重建视图；配置变量、日志读取器、子进程与停止请求继续由控制器持有。
        old = self.view
        log = self.log.get('1.0', 'end-1c')
        tags = {name: self.log.tag_ranges(name) for name in ('error', 'warning', 'success')}
        scroll = self.log.yview()[0]
        enabled = {name: getattr(self, name).state() for name in ('start_button', 'check_button', 'stop_button')}
        old.dispose()
        for child in self.window.winfo_children():
            child.destroy()
        self.theme = chosen
        self.view = WorkspaceView(self, FIELDS, initial=False)
        self.view.show(old.page)
        for name in ('phase', 'players', 'cloud'):
            getattr(self.view, name).set(getattr(old, name).get())
        self.log.configure(state='normal')
        self.log.insert('end', log)
        for name, ranges in tags.items():
            for start, end in zip(ranges[::2], ranges[1::2]):
                self.log.tag_add(name, str(start), str(end))
        self.log.configure(state='disabled')
        self.log.yview_moveto(scroll)
        for name, state in enabled.items():
            getattr(self, name).state(('!disabled', *state))
        self.status.set('主题已保存。')

    def load_config(self, path):
        args = parse_args(['--config', str(path)])
        for name in self.values:
            value = getattr(args, name)
            self.values[name].set(str(value) if value is not None else '')
        self.config_path = Path(path).resolve()
        self.location.set(str(self.config_path))

    def settings(self):
        settings = {}
        for name, _, _ in FIELDS:
            value = self.values[name].get().strip()
            if not value:
                raise ValueError('请填写所有路径字段')
            path = Path(value)
            settings[name] = str((path if path.is_absolute() else self.config_path.parent / path).resolve())
        settings['seconds'] = int(self.values['seconds'].get())
        settings['startup_timeout'] = float(self.values['startup_timeout'].get())
        if settings['seconds'] <= 0 or not math.isfinite(settings['startup_timeout']) or settings['startup_timeout'] <= 0:
            raise ValueError('时长和超时必须为正数')
        return settings

    def open_config(self):
        path = filedialog.askopenfilename(filetypes=[('配置文件', '*.json')])
        if path:
            try:
                self.load_config(path)
            except (Exception, SystemExit) as error:
                messagebox.showerror('配置无法读取', str(error))

    def save_config(self, save_as=False):
        try:
            settings = self.settings()
            path = self.config_path
            if save_as:
                chosen = filedialog.asksaveasfilename(defaultextension='.json', filetypes=[('配置文件', '*.json')])
                if not chosen:
                    return
                path = Path(chosen)
            atomic_write(path, json.dumps(settings, ensure_ascii=False, indent=2).encode('utf-8'))
            self.config_path = path
            self.location.set(str(path))
            self.status.set('配置已保存。')
        except Exception as error:
            messagebox.showerror('无法保存', str(error))

    def browse(self, name, kind):
        if kind == 'folder':
            path = filedialog.askdirectory()
        else:
            pattern = '*.w3x *.w3m' if name == 'map' else '*.lua'
            path = filedialog.askopenfilename(filetypes=[('地图' if name == 'map' else 'Lua 脚本', pattern), ('所有文件', '*.*')])
        if path:
            self.values[name].set(path)

    def append(self, text):
        self.log.configure(state='normal')
        following = self.log.yview()[1] >= .98
        for line in text.splitlines(keepends=True):
            tag = 'error' if any(key in line for key in ('ERROR', 'FATAL', 'FAILED', '故障', '失败')) else (
                'warning' if 'WARN' in line else 'success' if 'START_GAME' in line or '"ok": true' in line else '')
            self.log.insert('end', line, tag)
        lines = int(self.log.index('end-1c').split('.')[0])
        if lines > 3000:
            self.log.delete('1.0', f'{lines - 2500}.0')
        if following:
            self.log.see('end')
        self.log.configure(state='disabled')

    def start(self, check=False):
        if self.process is not None:
            return
        try:
            settings = self.settings()
            folder = ROOT / 'build/gui'
            folder.mkdir(parents=True, exist_ok=True)
            token = uuid.uuid4().hex
            config = folder / f'{token}.json'
            atomic_write(config, json.dumps(settings, ensure_ascii=False).encode('utf-8'))
            self.stop_file = folder / f'{token}.stop'
            command = worker_command() + ['--config', str(config), '--stop-file', str(self.stop_file)]
            if check:
                command.append('--check')
            if not self.separate_console.get():
                command.append('--no-console')
            self.close_cloud()
            self.checking = check
            self.client_pids.clear()
            self.view.show('overview')
            self.view.set_phase('检查中' if check else '启动中')
            if not check:
                self.view.players.set('正在启动')
                self.view.cloud.set('等待连接')
            self.process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding='utf-8', errors='replace', creationflags=subprocess.CREATE_NO_WINDOW)
            threading.Thread(target=self.read_worker, args=(self.process,), daemon=True).start()
            self.start_button.configure(state='disabled')
            self.check_button.configure(state='disabled')
            self.stop_button.configure(state='normal' if not check else 'disabled')
            self.status.set('正在检查配置…' if check else '正在启动两个客户端…')
            self.append('\n—— 检查配置 ——\n' if check else '\n—— 启动双人测试 ——\n')
        except Exception as error:
            self.view.set_phase('启动失败')
            self.append(f'启动失败：{error}\n')
            messagebox.showerror('启动失败', str(error))

    def read_worker(self, process):
        try:
            for line in process.stdout:
                self.messages.put(('line', line))
        finally:
            process.stdout.close()
            self.messages.put(('exit', process.wait()))

    def close_cloud(self):
        if self.cloud_file:
            self.cloud_file.close()
        self.cloud_file = self.cloud_path = self.decoder = None

    def poll(self):
        while not self.messages.empty():
            kind, value = self.messages.get_nowait()
            if kind == 'line':
                if value.startswith('CLOUD_CONSOLE '):
                    self.cloud_path = Path(value.removeprefix('CLOUD_CONSOLE ').strip())
                    try:
                        self.cloud_file = self.cloud_path.open('rb')
                        self.decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')
                    except OSError as error:
                        self.append(f'云日志暂时无法打开：{error}\n')
                if value.startswith('GAME_PID '):
                    self.client_pids.add(value.strip())
                    self.view.players.set(f'{len(self.client_pids)} / 2 已启动')
                if value.startswith('CLOUD_ACTIVE'):
                    self.view.cloud.set('运行中 · 共享房间')
                    self.view.set_phase('运行中')
                    self.status.set('共享云脚本运行中；请在两个客户端正常保存、退出后停止云服务。')
                if 'START_GAME players=2' in value:
                    self.status.set('两个客户端已开局，等待地图及云脚本就绪。')
                    self.view.players.set('2 / 2 已开局')
                if not value.startswith('CLOUD_ACTIVE'):
                    self.append(value)
            else:
                self.process = None
                self.view.set_phase(('检查通过' if self.checking else '已停止') if value == 0 else '操作失败')
                if not self.checking:
                    self.view.cloud.set('已停止')
                    self.view.players.set('请退出游戏窗口')
                self.start_button.configure(state='normal')
                self.check_button.configure(state='normal')
                self.stop_button.configure(state='disabled')
                self.status.set('操作完成；可查看日志。' if value == 0 else f'操作失败（退出码 {value}），请查看日志。')
                if self.closing:
                    self.close_cloud()
                    self.window.destroy()
                    return
        if self.cloud_file:
            data = self.cloud_file.read(65536)
            if data:
                self.append(self.decoder.decode(data))
            elif self.cloud_path.with_suffix('.done').exists():
                self.append(self.decoder.decode(self.cloud_file.read(), final=True))
                self.close_cloud()
        self.poll_job = self.window.after(150, self.poll)

    def stop(self):
        if self.process is not None and self.stop_file:
            self.stop_file.touch()
            self.status.set('正在停止云服务，游戏窗口请正常退出。')
            self.view.set_phase('正在停止')
            self.stop_button.configure(state='disabled')

    def open_logs(self):
        folder = ROOT / 'build/game'
        folder.mkdir(parents=True, exist_ok=True)
        os.startfile(folder)

    def open_archives(self):
        try:
            os.startfile(self.settings()['archives'])
        except Exception as error:
            messagebox.showerror('无法打开目录', str(error))

    def close(self):
        if self.process is not None:
            if not messagebox.askyesno('关闭测试工具', '关闭界面将停止云脚本服务；游戏窗口需自行退出。继续吗？'):
                return
            self.closing = True
            self.stop()
        else:
            self.close_cloud()
            self.window.destroy()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=ROOT / '本地测试配置.json')
    args = parser.parse_args(argv)
    # 在创建控件前启用 DPI 感知，避免 Windows 对整个窗口做位图拉伸。
    ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    window = tk.Tk()
    LauncherWindow(window, args.config)
    window.mainloop()
