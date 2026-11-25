"""Run with xvfb-run -a -s '-screen 0 1280x720x24' python3 <script> <editor>."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def window_info(*args):
    return subprocess.run(
        ["xwininfo", *args], capture_output=True, text=True, timeout=2, check=False
    )


def dimensions(info):
    return tuple(int(re.search(rf"^\s*{key}: (\d+)$", info, re.M)[1])
                 for key in ("Width", "Height"))


def wait_for_project_window(editor, title):
    deadline = time.monotonic() + 10
    while editor.poll() is None and time.monotonic() < deadline:
        info = window_info("-name", "3dmppc-editor - " + title, "-stats", "-size")
        # The project sets the window title before the first frame applies the size hints.
        minimum = re.search(r"Program supplied minimum size: (\d+) by (\d+)", info.stdout)
        if info.returncode == 0 and minimum:
            return dimensions(info.stdout), tuple(map(int, minimum.groups()))
        time.sleep(0.05)
    raise AssertionError(f"Project window did not become ready (editor exit status {editor.poll()})")


def test_saved_large_font_keeps_long_title_project_on_display(command):
    screen = dimensions(window_info("-root", "-stats").stdout)
    if screen != (1280, 720):
        raise AssertionError(f"Test requires a 1280x720 display, got {screen}")

    repo = Path(__file__).resolve().parents[2]
    header = (repo / "pdk/include/pdk/de/rv_dv.h").read_text()
    version = ".".join(re.search(rf"#define RV_MPPC_VER_{part} (\d+)", header)[1]
                       for part in ("MAJOR", "MINOR"))
    title = "A Long Project Title for Testing Editor Layout on a Small Desktop Screen"
    with tempfile.TemporaryDirectory(prefix="editor-window-fit-", dir=Path.cwd()) as directory:
        root = Path(directory)
        project = root / "project"
        project.mkdir()
        (project / "disc.toml").write_text(f'[disc]\nid = "window-fit"\ntitle = "{title}"\n')
        config = root / "config" / "3dmppc-editor"
        config.mkdir(parents=True)
        (config / "view").write_text(
            f"3dmppc-editor-view {version}\nui-scale 1\nmain-font large\n"
        )
        env = dict(os.environ, SDL_VIDEODRIVER="x11", SDL_RENDER_DRIVER="software",
                   XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"),
                   XDG_STATE_HOME=str(root / "state"))
        with subprocess.Popen([*command, project], env=env) as editor:
            try:
                size, minimum = wait_for_project_window(editor, title)
                if any(actual > limit for pair in (size, minimum)
                       for actual, limit in zip(pair, screen)):
                    raise AssertionError(
                        f"Project window {size[0]}x{size[1]} (minimum {minimum[0]}x{minimum[1]}) "
                        f"exceeds {screen[0]}x{screen[1]} display with saved large main font at 1x"
                    )
            finally:
                if editor.poll() is None:
                    editor.terminate()
                    try:
                        editor.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        editor.kill()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Pass the built 3dmppc-editor executable as an argument")
    test_saved_large_font_keeps_long_title_project_on_display(sys.argv[1:])
