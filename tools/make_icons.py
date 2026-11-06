#!/usr/bin/env python3
# HorusGram Desktop - renders the icon set of HORUSGRAM_DESIGN.md into
# Telegram/Resources/icons/hoursgram as the masks the client uses: white on
# black, 28x28 with the icon drawn at 18, and @2x / @3x.
#
# Usage: python tools/make_icons.py <Tools/horusgram/make_icons.py of the
#        Android repository> <out dir>
#
# The icons themselves are defined once, in the Android repository; they are
# drawn here by Microsoft Edge in headless mode and cut out with Pillow.
import importlib.util, os, subprocess, sys, tempfile
from PIL import Image

spec = importlib.util.spec_from_file_location('icons', sys.argv[1])
icons = importlib.util.module_from_spec(spec)
sys.argv_saved = sys.argv
sys.argv = [sys.argv[1], tempfile.mkdtemp()]  # the Android script writes its drawables there
spec.loader.exec_module(icons)
sys.argv = sys.argv_saved
out = sys.argv[2]
os.makedirs(out, exist_ok=True)

CANVAS, ICON = 28, 18
names = list(icons.ICONS)
cells = []
for row, scale in enumerate((1, 2, 3)):
    for col, name in enumerate(names):
        paths = []
        for data, fill in icons.ICONS[name]:
            f = '#fff' if fill else 'none'
            o = icons.DUO if fill == 1 else 1
            paths.append(f'<path d="{data}" fill="{f}" fill-opacity="{o}" stroke="#fff" '
                         f'stroke-width="{icons.LINE}" stroke-linecap="square" stroke-linejoin="miter"/>')
        size, glyph = CANVAS * scale, ICON * scale
        cells.append(f'<svg style="position:absolute;left:{col * 100 + (size - glyph) // 2}px;'
                     f'top:{row * 100 + (size - glyph) // 2}px" width="{glyph}" height="{glyph}" '
                     f'viewBox="0 0 24 24">{"".join(paths)}</svg>')
html = ('<html><body style="margin:0;background:#000;width:%dpx;height:300px">%s</body></html>'
        % (len(names) * 100, ''.join(cells)))
# Not tempfile: the Store build of Python keeps its temp folder where Edge
# cannot see it.
work = os.path.abspath(os.environ.get('HG_ICONS_WORK', 'out/hoursgram_icons'))
os.makedirs(work, exist_ok=True)
page = os.path.join(work, 'icons.html')
shot = os.path.join(work, 'icons.png')
open(page, 'w', encoding='utf-8').write(html)
edge = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
if os.path.exists(shot):
    os.remove(shot)
# A profile of its own, so that an Edge already open does not take the job.
subprocess.run([edge, '--headless=new', '--disable-gpu', '--hide-scrollbars',
                '--user-data-dir=' + os.path.join(work, 'profile'),
                '--force-device-scale-factor=1', f'--window-size={len(names) * 100},300',
                f'--screenshot={shot}', 'file:///' + page.replace(os.sep, '/')],
               check=True, capture_output=True)
import time
for _ in range(60):
    if os.path.exists(shot) and os.path.getsize(shot) > 0:
        break
    time.sleep(0.5)
time.sleep(0.5)
sheet = Image.open(shot).convert('RGB')
for row, (scale, suffix) in enumerate(((1, ''), (2, '@2x'), (3, '@3x'))):
    for col, name in enumerate(names):
        size = CANVAS * scale
        box = (col * 100, row * 100, col * 100 + size, row * 100 + size)
        sheet.crop(box).save(os.path.join(out, f'settings_{name}{suffix}.png'))
print(len(names), 'icons')
