"""
Obsidian Terminal — Desktop Entry Point
========================================
Launches the Obsidian Quantitative Platform dashboard in a native
WebView2 window. The entire UI is rendered via hand-crafted HTML/CSS/JS
for maximum visual fidelity.

Usage:
    python main.py          # Development mode (debug console enabled)
    ObsidianTerminal.exe    # Packaged release (see build.bat)
"""

import webview
import os
import sys


def _resource_path(relative: str) -> str:
    """Resolve a path that works in both dev and PyInstaller-bundled mode."""
    if getattr(sys, '_MEIPASS', None):
        base = sys._MEIPASS
    else:
        base = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, relative)


def main() -> None:
    html = _resource_path(os.path.join('app', 'index.html'))

    window = webview.create_window(
        title='Obsidian Terminal',
        url=html,
        width=1440,
        height=900,
        min_size=(1024, 640),
        background_color='#08080c',
        text_select=False,
    )

    # edgechromium = WebView2 on Windows 10/11
    webview.start(debug='--debug' in sys.argv, gui='edgechromium')


if __name__ == '__main__':
    main()
