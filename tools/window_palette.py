# Usage: python tools/window_palette.py Telegram/Resources/night.tdesktop-theme Telegram/Resources/hoursgram-window.tdesktop-palette
# Builds hoursgram-window.tdesktop-palette, the HorusGram WINDOW theme, from
# the night theme of Telegram. The look is monochrome: black panels, white
# for everything active (buttons, switches, counters) with black on it, soft
# grays for the rest. Colors that mean something stay: the colors of names
# and profiles, red for danger, green for calls and success.
import colorsys
import re
import sys
import zipfile

src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(src) as z:
    text = z.read('colors.tdesktop-theme').decode('utf-8')

KEEP = re.compile(
    r'Peer\d|Userpic|userpic|attention|Attention|call|Call|Green|Red|'
    r'Error|error|Danger|danger|Online|Verified|Premium|premium|Gift|gift|'
    r'Stars|stars|Credits|credits|mediaview|Mediaview|Ton|Wallet|wallet|'
    r'Record|record')

WHITE, BLACK = '#FFFFFF', '#000000'
KEYS = {
    # Surfaces: a black canvas, a little lighter for what is raised.
    'windowBg': '#000000', 'windowBgOver': '#111111',
    'windowBgRipple': '#1A1A1A', 'windowShadowFgFallback': '#000000',
    'boxBg': '#0F0F0F', 'boxSearchBg': '#0F0F0F', 'boxDividerBg': '#0A0A0A',
    'menuBg': '#0F0F0F', 'menuBgOver': '#1A1A1A', 'menuBgRipple': '#242424',
    'menuSeparatorFg': '#1A1A1A', 'menuIconFg': '#8C8C8C',
    'menuIconFgOver': '#FFFFFF', 'menuFgDisabled': '#4D4D4D',
    'titleBg': '#000000', 'titleBgActive': '#000000',
    'topBarBg': '#000000', 'dialogsBg': '#000000', 'mainMenuBg': '#000000',
    'sideBarBg': '#000000', 'sideBarBgActive': '#1A1A1A',
    'sideBarBgRipple': '#141414',
    'inputBorderFg': '#242424', 'checkboxFg': '#4D4D4D',
    # Text.
    'windowFg': '#FFFFFF', 'windowFgOver': '#FFFFFF',
    'windowBoldFg': '#FFFFFF', 'windowBoldFgOver': '#FFFFFF',
    'windowSubTextFg': '#8C8C8C', 'windowSubTextFgOver': '#A6A6A6',
    'dialogsNameFg': '#FFFFFF', 'dialogsTextFg': '#8C8C8C',
    'dialogsTextFgOver': '#A6A6A6', 'dialogsTextFgService': '#FFFFFF',
    'dialogsTextFgServiceOver': '#FFFFFF', 'boxTitleFg': '#FFFFFF',
    'placeholderFg': '#737373', 'placeholderFgActive': '#4D4D4D',
    'introTitleFg': '#FFFFFF', 'introDescriptionFg': '#8C8C8C',
    # What is active is white, with black on it.
    'windowBgActive': WHITE, 'windowFgActive': BLACK,
    'windowActiveTextFg': WHITE, 'activeLineFg': WHITE,
    'activeButtonBg': WHITE, 'activeButtonBgOver': '#E6E6E6',
    'activeButtonBgRipple': '#CCCCCC', 'activeButtonFg': BLACK,
    'activeButtonFgOver': BLACK, 'activeButtonSecondaryFg': '#404040',
    'activeButtonSecondaryFgOver': '#404040',
    'lightButtonBg': '#0F0F0F', 'lightButtonBgOver': '#1A1A1A',
    'lightButtonBgRipple': '#242424', 'lightButtonFg': WHITE,
    'outlineButtonOutlineFg': WHITE, 'outlineButtonBgOver': '#111111',
    'outlineButtonBgRipple': '#1A1A1A',
    'mainMenuCoverBg': WHITE, 'mainMenuCoverFg': BLACK,
    'mainMenuCloudBg': '#242424', 'mainMenuCloudFg': WHITE,
    'emojiIconFg': '#8C8C8C', 'emojiIconFgActive': WHITE,
    'sideBarIconFg': '#737373', 'sideBarIconFgActive': WHITE,
    'sideBarTextFg': '#737373', 'sideBarTextFgActive': WHITE,
    'sideBarBadgeBg': WHITE, 'sideBarBadgeBgMuted': '#4D4D4D',
    'sideBarBadgeFg': BLACK,
    # The chat list: the chosen chat is a lighter plate, counters are white.
    'dialogsBgOver': '#0D0D0D', 'dialogsRippleBg': '#1A1A1A',
    'dialogsBgActive': '#1A1A1A', 'dialogsRippleBgActive': '#242424',
    'dialogsNameFgActive': WHITE, 'dialogsTextFgActive': '#A6A6A6',
    'dialogsTextFgServiceActive': WHITE,
    'dialogsUnreadBg': WHITE, 'dialogsUnreadBgOver': WHITE,
    'dialogsUnreadFg': BLACK, 'dialogsUnreadFgOver': BLACK,
    'dialogsUnreadBgActive': WHITE, 'dialogsUnreadFgActive': BLACK,
    'dialogsUnreadBgMuted': '#3A3A3A', 'dialogsUnreadBgMutedOver': '#404040',
    'dialogsUnreadBgMutedActive': '#4D4D4D',
    # The field is a raised pill, the send button is white.
    'historyComposeAreaBg': '#141414', 'historyComposeButtonBg': '#141414',
    'historyComposeButtonBgOver': '#1A1A1A',
    'historyComposeButtonBgRipple': '#242424',
    'historyComposeIconFg': '#8C8C8C', 'historyComposeIconFgOver': WHITE,
    'historySendIconFg': WHITE, 'historySendIconFgOver': '#E6E6E6',
    'historyReplyBg': '#0F0F0F',
    # Messages: incoming on the card, own a step lighter.
    'msgInBg': '#141414', 'msgInBgSelected': '#2A2A2A',
    'msgOutBg': '#262626', 'msgOutBgSelected': '#383838',
    'msgInShadow': '#00000000', 'msgOutShadow': '#00000000',
    # Links stay readable in a calm blue.
    'historyLinkInFg': '#60A5FA', 'historyLinkOutFg': '#93C5FD',
    'historyLinkInFgSelected': '#93C5FD',
    'historyLinkOutFgSelected': '#BFDBFE',
    # Danger and success.
    'attentionButtonFg': '#EF4444', 'attentionButtonFgOver': '#F87171',
    'attentionButtonBgOver': '#2A1010', 'attentionButtonBgRipple': '#3A1414',
    'boxTextFgError': '#EF4444', 'activeLineFgError': '#EF4444',
    'introErrorFg': '#EF4444', 'boxTextFgGood': '#10B981',
    'profileVerifiedCheckBg': WHITE, 'profileVerifiedCheckFg': BLACK,
    'introCoverTopBg': '#0A0A0A', 'introCoverBottomBg': '#141414',
}


def gray(color, alpha):
    r, g, b = (int(color[i:i + 2], 16) / 255. for i in (0, 2, 4))
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    # The night theme is blue: those hues turn neutral and darks go deeper,
    # any other hue is a color with a meaning and stays.
    if s > 0.08 and not (0.47 <= h <= 0.70):
        return '#' + color + alpha
    if l < 0.3:
        l *= 0.55
    v = round(l * 255)
    return '#%02X%02X%02X' % (v, v, v) + alpha


def grayed(value):
    return re.sub(
        r'#([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?',
        lambda m: gray(m.group(1), m.group(2) or ''),
        value)


out = [
    '// HorusGram WINDOW: the usual Telegram client in a monochrome look,',
    '// black panels and white for what is active. Built by',
    '// tools/window_palette.py from the night theme of Telegram; the parts',
    '// are recolored in Customization, see customization_palette.cpp.',
]
seen = set()
for line in text.replace('\r\n', '\n').split('\n'):
    m = re.match(r'^(\w+):\s*(.*?);(.*)$', line)
    if not m:
        if line.strip():
            out.append(line)
        continue
    key, value, rest = m.groups()
    seen.add(key)
    if key in KEYS:
        alpha = value[7:9] if (value.startswith('#') and len(value) == 9) else ''
        value = KEYS[key] + ('' if len(KEYS[key]) == 9 else alpha)
    elif not KEEP.search(key):
        value = grayed(value)
    out.append(f'{key}: {value};{rest}')
out += [f'{k}: {v};' for k, v in KEYS.items() if k not in seen]
out.append('')
open(dst, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print(len(seen), 'keys,', len([k for k in KEYS if k not in seen]), 'added')
