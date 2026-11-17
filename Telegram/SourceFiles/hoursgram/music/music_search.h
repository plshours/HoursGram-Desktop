/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace HoursGram::Music {

// The music search window: Telegram tracks from all chats plus SoundCloud,
// played through the built-in player.
void ShowSearch(not_null<Window::SessionController*> controller);

} // namespace HoursGram::Music
