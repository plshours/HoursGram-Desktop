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

namespace HoursGram::Plugins {

// The installed plugins: turn them on and off, set them up, remove them,
// and install new ones from a file.
void ShowPluginsBox(not_null<Window::SessionController*> controller);

} // namespace HoursGram::Plugins
