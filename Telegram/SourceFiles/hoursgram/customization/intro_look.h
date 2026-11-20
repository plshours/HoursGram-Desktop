/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace HoursGram::Customization {

// The look of the login screen, as set in Customization: the background
// (plain like Telegram's, a gradient, or a slow glow), a card the form sits
// on, and a title above the card.

// Whether anything is drawn besides the plain Telegram background.
[[nodiscard]] bool IntroDecorated();

// The glow moves, so whoever paints it has to repaint now and then.
[[nodiscard]] bool IntroAnimated();

// The card around a form whose content starts at `contentTop`: the same
// box for every step, so the card stays put while the steps slide.
[[nodiscard]] QRect IntroCardRect(QSize outer, int contentTop);

// Paints the background, the card and the title for a screen of `outer`
// size. `now` moves the glow.
void PaintIntro(
	QPainter &p,
	QSize outer,
	int contentTop,
	crl::time now);

// Everything the login screen look has, with a live preview: the box goes
// to Customization and to the settings of the login screen.
void IntroLookBox(not_null<Ui::GenericBox*> box);

} // namespace HoursGram::Customization
