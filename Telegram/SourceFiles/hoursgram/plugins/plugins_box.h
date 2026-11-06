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

// The list of installed plugins: turn on and off, set up, delete, install
// from a file.
void ShowPluginsBox(not_null<Window::SessionController*> controller);

void ShowPluginSettingsBox(
	not_null<Window::SessionController*> controller,
	const QString &id);

// Opening a .hgplugin file from a chat asks before installing it. Returns
// false when the file is not a plugin, so that it opens the usual way.
bool AskInstall(
	not_null<Window::SessionController*> controller,
	const QString &path);

} // namespace HoursGram::Plugins
