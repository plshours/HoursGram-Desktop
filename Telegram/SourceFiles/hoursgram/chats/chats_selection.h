/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HoursGram::Chats {

// Chats are picked in the chat list the way phones do it: with Shift and a
// click, or "Select" in the menu of a chat, and then with a plain click.
// This bar floats over the bottom of the list while any are picked, says
// how many and reads or deletes all of them at once.
[[nodiscard]] not_null<Ui::RpWidget*> CreateSelectionBar(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<int> count,
	Fn<std::vector<not_null<History*>>()> selected,
	Fn<void()> clear);

// The mark over the bottom right corner of the userpic of a picked chat.
void PaintSelectionCheck(QPainter &p, QRect userpic);

} // namespace HoursGram::Chats
