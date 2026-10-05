"""启动器外观偏好独立保存，不进入地图启动配置。"""
import json
from types import SimpleNamespace
from tools.client_archives import atomic_write

THEMES = {'monochrome': '黑白灰', 'shadowworks': '深灰蓝'}
DEFAULT_THEME = 'monochrome'

_BASE = dict(BG='#202020', PANEL='#262626', INK='#eeeeee', MUTED='#aaaaaa', LINE='#3e3e3e',
             ACCENT='#eeeeee', ACCENT_TEXT='#161616', ACCENT_HOVER='#ffffff', ACCENT_DISABLED='#444444',
             NAV='#181818', NAV_TEXT='#bbbbbb', NAV_HOVER='#333333', NAV_SELECTED='#3b3b3b',
             BUTTON='#303030', HOVER='#444444', DISABLED='#777777', ENTRY='#1d1d1d',
             SELECTION='#4a4a4a', ERROR='#ffffff', WARNING='#cccccc', SUCCESS='#eeeeee')


def palette(theme):
    colors = dict(_BASE)
    if theme == 'shadowworks':
        colors.update(INK='#e3e5e8', MUTED='#a6acb5', LINE='#3e4146', ACCENT='#1b62c8',
                      ACCENT_TEXT='#eeeeee', ACCENT_HOVER='#2874df', ACCENT_DISABLED='#294467',
                      NAV='#124493', NAV_TEXT='#c2d3ee', NAV_HOVER='#235bac', NAV_SELECTED='#2f64ad',
                      BUTTON='#2f3237', SELECTION='#233c5b', ERROR='#ff8d95', WARNING='#d4b377', SUCCESS='#82c7a5')
    return SimpleNamespace(**colors)


def load_theme(path):
    try:
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        name = data.get('theme') if isinstance(data, dict) else None
        return name if isinstance(name, str) and name in THEMES else DEFAULT_THEME
    except (OSError, ValueError):
        return DEFAULT_THEME


def save_theme(path, name):
    if name not in THEMES:
        raise ValueError('未知主题')
    atomic_write(path, json.dumps({'theme': name}, ensure_ascii=False, indent=2).encode('utf-8'))
