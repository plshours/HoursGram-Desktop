/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HoursGram::Customization {

// A named color of the palette a slot sets. `shift` makes the shades that go
// with it: +8 moves the color 8% away from itself (to white when it is dark,
// to black when it is light) for a hover, -10 moves it 10% to black.
struct PaletteKey {
	QString key;
	int shift = 0;
};

// One editable color of the app palette. A slot without `keys` stands for
// every place the shipped palette uses `base`, so changing it moves all of
// them at once and the palette stays consistent with itself. A slot with
// `keys` sets just those palette colors, so one part of the client (the
// buttons, the input field, the menu) changes and the rest stays.
struct PaletteSlot {
	QString id;
	QString base;
	Fn<rpl::producer<QString>()> title;
	std::vector<PaletteKey> keys;
	// The heading the slot is listed under; a slot without one goes on
	// under the heading of the slot before it.
	Fn<rpl::producer<QString>()> group;
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
