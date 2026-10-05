"""桌面工作台视图：概览与项目设置分离，启动后端由控制器持有。"""
import ctypes
from ctypes import wintypes
import tkinter as tk
from tkinter import ttk

from tools.launcher_theme import THEMES, palette

FONT = 'Microsoft YaHei UI'


def label(parent, text='', size=10, color=None, bold=False, **kwargs):
    color = color or parent.winfo_toplevel().palette.INK
    return tk.Label(parent, text=text, bg=parent.cget('bg'), fg=color,
                    font=(FONT, size, 'bold' if bold else 'normal'), **kwargs)


def card(parent, **kwargs):
    c = parent.winfo_toplevel().palette
    return tk.Frame(parent, bg=c.PANEL, highlightbackground=c.LINE, highlightthickness=1, **kwargs)


def configure_style(window, c):
    style = ttk.Style(window)
    style.theme_use('clam')
    style.configure('.', font=(FONT, 10))
    style.configure('TButton', padding=(12, 7), background=c.BUTTON, foreground=c.INK,
                    bordercolor=c.LINE, lightcolor=c.PANEL, darkcolor=c.PANEL, focuscolor=c.ACCENT)
    style.map('TButton', background=[('disabled', c.BG), ('active', c.HOVER)],
              foreground=[('disabled', c.DISABLED)], bordercolor=[('focus', c.ACCENT)])
    style.configure('Primary.TButton', background=c.ACCENT, foreground=c.ACCENT_TEXT, bordercolor=c.ACCENT,
                    lightcolor=c.ACCENT, darkcolor=c.ACCENT, font=(FONT, 10, 'bold'))
    style.map('Primary.TButton', background=[('disabled', c.ACCENT_DISABLED), ('active', c.ACCENT_HOVER)],
              foreground=[('disabled', c.DISABLED), ('!disabled', c.ACCENT_TEXT)])
    style.configure('TEntry', padding=(10, 7), fieldbackground=c.ENTRY, foreground=c.INK,
                    bordercolor=c.LINE, lightcolor=c.LINE, darkcolor=c.LINE, insertcolor=c.INK)
    style.map('TEntry', bordercolor=[('focus', c.ACCENT)],
              fieldbackground=[('readonly', c.ENTRY)], foreground=[('readonly', c.MUTED)])
    style.configure('TCheckbutton', background=c.PANEL, foreground=c.MUTED, padding=4)
    style.map('TCheckbutton', background=[('active', c.PANEL)])
    style.configure('Vertical.TScrollbar', background=c.HOVER, troughcolor=c.BG,
                    bordercolor=c.BG, arrowcolor=c.MUTED, width=10)
    style.configure('TCombobox', padding=(10, 7), fieldbackground=c.ENTRY, foreground=c.INK,
                    background=c.BUTTON, arrowcolor=c.INK, bordercolor=c.LINE)
    style.map('TCombobox', fieldbackground=[('readonly', c.ENTRY)], foreground=[('readonly', c.INK)],
              selectbackground=[('readonly', c.ENTRY)], selectforeground=[('readonly', c.INK)])
    window.option_add('*TCombobox*Listbox.background', c.ENTRY)
    window.option_add('*TCombobox*Listbox.foreground', c.INK)
    window.option_add('*TCombobox*Listbox.selectBackground', c.SELECTION)
    window.option_add('*TCombobox*Listbox.selectForeground', c.INK)


class WorkspaceView:
    def __init__(self, controller, fields, initial=True):
        self.app = controller
        self.window = controller.window
        self.fields = fields
        self.c = palette(controller.theme)
        self.window.palette = self.c
        self.page = 'overview'
        self.phase = tk.StringVar(value='未运行')
        self.players = tk.StringVar(value='等待启动')
        self.cloud = tk.StringVar(value='未启动')
        self.configure_window(initial)
        self.build_navigation()
        self.build_workspace()
        self.build_overview()
        self.build_settings()
        self.show('overview')
        self.window.bind('<MouseWheel>', self.scroll_settings)

    def configure_window(self, initial):
        self.window.title('ScriptRoom')
        self.window.configure(bg=self.c.BG)
        scale = max(1, float(self.window.tk.call('tk', 'scaling')) / (96 / 72))
        self.scale = scale
        width = min(round(1180 * scale), self.window.winfo_screenwidth() - 80)
        height = min(round(820 * scale), self.window.winfo_screenheight() - 100)
        if initial:
            self.window.geometry(f'{width}x{height}')
        self.window.minsize(min(width, round(980 * scale)), min(height, round(640 * scale)))
        configure_style(self.window, self.c)
        self.caption_job = self.window.after_idle(self.apply_caption)

    def dispose(self):
        self.window.after_cancel(self.caption_job)

    def apply_caption(self):
        # 保留系统标题栏的拖动与缩放，同时使配色与深色工作区一致。
        user32 = ctypes.windll.user32
        user32.GetParent.argtypes = [wintypes.HWND]
        user32.GetParent.restype = wintypes.HWND
        handle = user32.GetParent(self.window.winfo_id())
        setter = ctypes.windll.dwmapi.DwmSetWindowAttribute
        setter.argtypes = [wintypes.HWND, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
        enabled = ctypes.c_int(1)
        setter(handle, 20, ctypes.byref(enabled), ctypes.sizeof(enabled))

    def build_navigation(self):
        side = tk.Frame(self.window, bg=self.c.NAV, width=round(64 * self.scale))
        side.pack(side='left', fill='y')
        side.pack_propagate(False)
        tk.Frame(side, bg=self.c.NAV, height=16).pack()
        self.nav_buttons = {}
        for page, title in [('overview', '运行'), ('settings', '设置')]:
            button = tk.Button(side, text=title, font=(FONT, 10), relief='flat',
                               bg=self.c.NAV, fg=self.c.NAV_TEXT, activebackground=self.c.NAV_HOVER, activeforeground=self.c.INK,
                               borderwidth=0, pady=13, cursor='hand2',
                               command=lambda name=page: self.show(name))
            button.pack(fill='x', padx=5, pady=4)
            self.nav_buttons[page] = button
        tk.Button(side, text='存档', font=(FONT, 9), relief='flat', bg=self.c.NAV, fg=self.c.NAV_TEXT,
                  activebackground=self.c.NAV_HOVER, activeforeground=self.c.INK, borderwidth=0, pady=12,
                  command=self.app.open_archives).pack(side='bottom', fill='x', padx=5, pady=16)

    def build_workspace(self):
        main = tk.Frame(self.window, bg=self.c.BG)
        main.pack(fill='both', expand=True)
        header = tk.Frame(main, bg=self.c.PANEL, padx=24, pady=14)
        header.pack(fill='x')
        title = tk.Frame(header, bg=self.c.PANEL)
        title.pack(side='left', fill='x', expand=True)
        label(title, 'ScriptRoom', size=15, bold=True).pack(anchor='w')
        ttk.Button(header, text='打开配置', command=self.app.open_config).pack(side='right')
        tk.Frame(main, bg=self.c.LINE, height=1).pack(fill='x')
        self.pages = tk.Frame(main, bg=self.c.BG, padx=24, pady=18)
        self.pages.pack(fill='both', expand=True)
        self.pages.rowconfigure(0, weight=1)
        self.pages.columnconfigure(0, weight=1)
        self.overview = tk.Frame(self.pages, bg=self.c.BG)
        self.settings = tk.Frame(self.pages, bg=self.c.BG)
        for frame in (self.overview, self.settings):
            frame.grid(row=0, column=0, sticky='nsew')
        footer = tk.Frame(main, bg=self.c.PANEL, padx=24, pady=8)
        footer.pack(fill='x', side='bottom')
        label(footer, color=self.c.MUTED, size=9, textvariable=self.app.status, anchor='w').pack(fill='x')

    def build_overview(self):
        actions = tk.Frame(self.overview, bg=self.c.BG)
        actions.pack(fill='x', pady=(0, 16))
        self.app.start_button = ttk.Button(actions, text='启动', style='Primary.TButton', command=self.app.start)
        self.app.start_button.pack(side='left')
        self.app.check_button = ttk.Button(actions, text='检查配置', command=lambda: self.app.start(True))
        self.app.check_button.pack(side='left', padx=10)
        self.app.stop_button = ttk.Button(actions, text='停止', state='disabled', command=self.app.stop)
        self.app.stop_button.pack(side='left')
        self.badge = label(actions, size=9, color=self.c.MUTED, textvariable=self.phase)
        self.badge.pack(side='right')
        metrics = tk.Frame(self.overview, bg=self.c.BG)
        metrics.pack(fill='x', pady=(0, 14))
        for name, variable in [('客户端', self.players), ('云脚本', self.cloud)]:
            label(metrics, name, 9, self.c.MUTED).pack(side='left', padx=(0, 8))
            label(metrics, size=9, textvariable=variable).pack(side='left', padx=(0, 24))
        self.build_logs()

    def build_logs(self):
        logs = card(self.overview)
        logs.pack(fill='both', expand=True)
        bar = tk.Frame(logs, bg=self.c.PANEL, padx=18, pady=10)
        bar.pack(fill='x')
        label(bar, '云端日志', 11, bold=True).pack(side='left')

        ttk.Button(bar, text='打开日志目录', command=self.app.open_logs).pack(side='right')
        tk.Frame(logs, bg=self.c.LINE, height=1).pack(fill='x')
        body = tk.Frame(logs, bg=self.c.ENTRY)
        body.pack(fill='both', expand=True)
        self.app.log = tk.Text(body, wrap='word', state='disabled', bg=self.c.ENTRY, fg=self.c.INK,
                              font=('Consolas', 10), relief='flat', borderwidth=0, highlightthickness=0,
                              padx=18, pady=14, height=7, selectbackground=self.c.SELECTION, insertbackground=self.c.INK)
        self.app.log.tag_configure('error', foreground=self.c.ERROR, font=('Consolas', 10, 'bold'))
        self.app.log.tag_configure('warning', foreground=self.c.WARNING)
        self.app.log.tag_configure('success', foreground=self.c.SUCCESS)
        scrollbar = ttk.Scrollbar(body, command=self.app.log.yview)
        self.app.log.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side='right', fill='y')
        self.app.log.pack(fill='both', expand=True)

    def build_settings(self):
        actions = tk.Frame(self.settings, bg=self.c.BG)
        actions.pack(fill='x', pady=(0, 15))
        ttk.Button(actions, text='保存配置', style='Primary.TButton', command=self.app.save_config).pack(side='left')
        ttk.Button(actions, text='另存为…', command=lambda: self.app.save_config(True)).pack(side='left', padx=10)
        ttk.Button(actions, text='打开存档目录', command=self.app.open_archives).pack(side='right')
        self.canvas = tk.Canvas(self.settings, bg=self.c.BG, highlightthickness=0)
        scroll = ttk.Scrollbar(self.settings, command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y')
        self.canvas.pack(fill='both', expand=True)
        content = tk.Frame(self.canvas, bg=self.c.BG)
        container = self.canvas.create_window((0, 0), window=content, anchor='nw')
        self.canvas.bind('<Configure>', lambda event: self.canvas.itemconfigure(container, width=event.width))
        content.bind('<Configure>', lambda event: self.canvas.configure(scrollregion=self.canvas.bbox('all')))
        appearance = self.settings_section(content, '外观', '选择后立即生效，自动保存。')
        self.theme_picker = ttk.Combobox(appearance, textvariable=self.app.theme_label,
                                        values=list(THEMES.values()), state='readonly', width=24)
        self.theme_picker.pack(anchor='w')
        self.theme_picker.bind('<<ComboboxSelected>>', self.app.change_theme)
        for title, description, names in [
            ('地图与脚本', '选择现有地图、客户端入口和云端脚本。', ('map', 'local_entry', 'cloud_scripts')),
            ('游戏与存档', '两个客户端共用测试魔兽目录，存档按玩家区分。', ('archives', 'game')),
            ('运行环境', '提供魔兽原始资源与编辑器插件。', ('editor', 'source_game')),
        ]:
            block = self.settings_section(content, title, description, collapsed=title == '运行环境')
            for name, caption, kind in self.fields:
                if name in names:
                    self.path_row(block, name, caption, kind)
        block = self.settings_section(content, '运行选项', '修改在下次启动生效。', collapsed=True)
        row = tk.Frame(block, bg=self.c.PANEL)
        row.pack(fill='x')
        for name, caption in [('seconds', '云服务时长（秒）'), ('startup_timeout', '开局超时（秒）')]:
            label(row, caption, 9, self.c.MUTED).pack(side='left', padx=(0, 6))
            ttk.Entry(row, textvariable=self.app.values[name], width=8).pack(side='left', padx=(0, 18))
        ttk.Checkbutton(block, text='另开独立云脚本控制台', variable=self.app.separate_console).pack(anchor='w', pady=(12, 0))
        label(content, '当前配置文件', 9, self.c.MUTED).pack(anchor='w', pady=(4, 5))
        ttk.Entry(content, textvariable=self.app.location, state='readonly').pack(fill='x', pady=(0, 12))

    def settings_section(self, parent, title, description, collapsed=False):
        section = card(parent, padx=20, pady=16)
        section.pack(fill='x', pady=(0, 14))
        header = tk.Frame(section, bg=self.c.PANEL)
        header.pack(fill='x')
        label(header, title, 11, bold=True).pack(side='left')
        body = tk.Frame(section, bg=self.c.PANEL)
        label(body, description, 9, self.c.MUTED).pack(anchor='w', pady=(8, 10))
        if collapsed:
            button = ttk.Button(header, text='展开')
            def toggle():
                if body.winfo_manager():
                    body.pack_forget()
                    button.configure(text='展开')
                else:
                    body.pack(fill='x')
                    button.configure(text='收起')
            button.configure(command=toggle)
            button.pack(side='right')
        else:
            body.pack(fill='x')
        return body

    def path_row(self, parent, name, caption, kind):
        row = tk.Frame(parent, bg=self.c.PANEL)
        row.pack(fill='x', pady=5)
        row.columnconfigure(1, weight=1)
        label(row, caption, 10, anchor='w', width=16).grid(row=0, column=0, sticky='w')
        ttk.Entry(row, textvariable=self.app.values[name]).grid(row=0, column=1, sticky='ew')
        ttk.Button(row, text='选择', command=lambda: self.app.browse(name, kind)).grid(row=0, column=2, padx=(8, 0))

    def show(self, page):
        self.page = page
        (self.overview if page == 'overview' else self.settings).tkraise()
        for name, button in self.nav_buttons.items():
            button.configure(bg=self.c.NAV_SELECTED if name == page else self.c.NAV, fg=self.c.INK if name == page else self.c.NAV_TEXT)

    def set_phase(self, text, color=None):
        self.phase.set(text)
        self.badge.configure(fg=color or self.c.INK)

    def scroll_settings(self, event):
        if self.page == 'settings':
            self.canvas.yview_scroll(-int(event.delta / 120), 'units')
