# Usage: python tools/graphite_palette.py Telegram/Resources/hoursgram.tdesktop-palette Telegram/Resources/hoursgram-graphite.tdesktop-palette
# Builds hoursgram-graphite.tdesktop-palette from hoursgram.tdesktop-palette.
import re, sys
src, dst = sys.argv[1], sys.argv[2]
COLORS = {  # HorusGram -> graphite
 '343641':'181818', '282A34':'121212', '3D404C':'232323', '464855':'2E2E2E',
 '46485B':'2E2E2E', '4F5268':'383838', '565B7B':'3E3E3E', '6E74A0':'555555',
 '626875':'7A7A7A', '505991':'3A3A3A', '777EB1':'6E6E6E',
 # The accent is a calm blue: buttons, toggles, links and indicators.
 '5A6090':'4F80EA', '646AA0':'5B8CF5', '6F76AE':'6F9BF7',
 '8993D8':'5B8CF5', 'AAB2ED':'8EB2FA', 'B1B2B7':'B8B8B8',
 '7B86CF':'2E2E2E', '717CC1':'33456B',
}
DARK = 'FFFFFF'  # text and icons that sit on the accent
ON_ACCENT = {'windowFgActive','activeButtonFg','activeButtonFgOver',
 'dialogsUnreadFg','dialogsUnreadFgOver','dialogsUnreadFgActive',
 'dialogsVerifiedIconFg','dialogsVerifiedIconFgOver','dialogsVerifiedIconFgActive',
 'profileVerifiedCheckFg','trayCounterFg','sideBarBadgeFg'}
KEYS = {  # the green call button stays a call button, in gray
 'callAnswerBg':'#5A5A5A', 'callAnswerRipple':'#6A6A6A', 'callAnswerBgOuter':'#5A5A5A26',
 # what is chosen or own gets a deep blue tint instead of plain gray
 'dialogsBgActive':'#22304C', 'dialogsRippleBgActive':'#2C3D60',
 'sideBarBgActive':'#22304C', 'msgOutBg':'#243352', 'msgOutBgSelected':'#2D3F64',
 'attentionButtonBgRipple':'#4A2C30',
 # dangerous actions (delete, block, leave) in a soft coral red, not white
 'attentionButtonFg':'#E06C75', 'attentionButtonFgOver':'#E8818A',
 'attentionButtonBgOver':'#3A2427'}
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
