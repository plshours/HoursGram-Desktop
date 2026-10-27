/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HoursGram::Customization {

// One editable color of the app palette. Each slot stands for every place
// the shipped palette uses `base`, so changing a slot moves all of them at
// once and the palette stays consistent with itself.
struct PaletteSlot {
	QString id;
	QString base;
	Fn<rpl::producer<QString>()> title;
};

[[nodiscard]] const std::vector<PaletteSlot> &PaletteSlots();

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
