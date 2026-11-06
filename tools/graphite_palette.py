# Usage: python tools/graphite_palette.py Telegram/Resources/hoursgram.tdesktop-palette Telegram/Resources/hoursgram-graphite.tdesktop-palette
# Builds hoursgram-graphite.tdesktop-palette from hoursgram.tdesktop-palette.
import re, sys
src, dst = sys.argv[1], sys.argv[2]
COLORS = {  # HorusGram -> graphite
 '343641':'181818', '282A34':'121212', '3D404C':'232323', '464855':'2E2E2E',
 '46485B':'2E2E2E', '4F5268':'383838', '565B7B':'3E3E3E', '6E74A0':'555555',
 '626875':'7A7A7A', '505991':'3A3A3A', '777EB1':'6E6E6E',
 '5A6090':'E8E6E1', '646AA0':'F4F2EE', '6F76AE':'D9D7D2',
 '8993D8':'E8E6E1', 'AAB2ED':'F7F6F2', 'B1B2B7':'B8B8B8',
 '7B86CF':'2E2E2E', '717CC1':'3A3A3A',
}
DARK = '1A1A1A'  # text and icons that sit on the light accent
ON_ACCENT = {'windowFgActive','activeButtonFg','activeButtonFgOver',
 'dialogsUnreadFg','dialogsUnreadFgOver','dialogsUnreadFgActive',
 'dialogsVerifiedIconFg','dialogsVerifiedIconFgOver','dialogsVerifiedIconFgActive',
 'profileVerifiedCheckFg','trayCounterFg','sideBarBadgeFg'}
KEYS = {  # the green call button stays a call button, in gray
 'callAnswerBg':'#5A5A5A', 'callAnswerRipple':'#6A6A6A', 'callAnswerBgOuter':'#5A5A5A26'}
EXTRA = {'walletTitleButtonCloseFgOver':'#FFFFFF','walletTitleButtonCloseFgActiveOver':'#FFFFFF','sideBarBadgeFg':'#'+DARK}
out, seen = [], set()
for line in open(src, encoding='utf-8').read().split('\n'):
    m = re.match(r'^(\w+):\s*(.*?);(.*)$', line)
    if not m:
        out.append(line); continue
    key, value, rest = m.groups(); seen.add(key)
    if key in KEYS:
        value = KEYS[key]
    elif key in ON_ACCENT and value.startswith('#'):
        value = '#' + DARK + value[7:]
    else:
        value = re.sub(r'#([0-9A-Fa-f]{6})', lambda c: '#' + COLORS.get(c.group(1).upper(), c.group(1)), value)
    out.append(f'{key}: {value};{rest}')
while out and not out[-1].strip():
    out.pop()
out += [f'{k}: {v};' for k, v in EXTRA.items() if k not in seen]
out.append('')
open(dst, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
