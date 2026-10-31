/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

namespace HoursGram::Customization {

// One editable color of the app palette. Each slot stands for every place
// the shipped palette uses `base`, so changing a slot moves all of them at
// once and the palette stays consistent with itself. The `followers` are
// the shades the palette derives from `base` (hover, pressed, selected), and
// they are moved by the same amount, so that no part of the old color stays
// behind.
struct PaletteSlot {
	QString id;
	QString base;
	std::vector<QString> followers;
	Fn<rpl::producer<QString>()> title;
};

[[nodiscard]] const std::vector<PaletteSlot> &PaletteSlots();

// A single key of the palette, like `msgOutBg`, that can be given a color
// of its own on top of what the slots produce.
struct PaletteKey {
	QString key;
	Fn<rpl::producer<QString>()> title;
};

struct PaletteGroup {
	QString id;
	Fn<rpl::producer<QString>()> title;
	std::vector<PaletteKey> keys;
};

[[nodiscard]] const std::vector<PaletteGroup> &PaletteGroups();

// Every key the shipped palette of the current theme defines, in its order.
[[nodiscard]] std::vector<QString> PaletteKeys();

// The id a key override is saved under for the current theme.
[[nodiscard]] QString KeyOverrideId(const QString &key);

// Whether a saved override id belongs to the current theme.
[[nodiscard]] bool IsCurrentThemeOverride(const QString &id);

// The colors the keys end up with right now, slots and overrides included,
// as "RRGGBB" or "RRGGBBAA". A key the palette does not define is missing.
[[nodiscard]] base::flat_map<QString, QString> CurrentKeyColors();

// "RRGGBB", or "RRGGBBAA" when the color is not opaque.
[[nodiscard]] QString ColorToHex(const QColor &color);
[[nodiscard]] QColor HexToColor(const QString &hex);

// Rebuilds the palette from the shipped one plus the saved picks and hands
// it to the running theme. With nothing overridden there is nothing to do
// at startup, so the swap is skipped unless `force` asks for it - which is
// what a picker needs when the pick it just saved equals the shipped color.
void ApplyCustomPalette(bool force = false);

// Puts the shipped palette back, ignoring the saved picks. This is a plain
// palette swap: the theme and the chat background are left alone, which is
// what keeps a reset from throwing the user back to Telegram's own default
// wallpaper.
void ResetToShippedPalette();

} // namespace HoursGram::Customization
