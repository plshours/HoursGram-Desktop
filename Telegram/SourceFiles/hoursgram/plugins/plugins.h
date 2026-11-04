/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

// HorusGram plugins: single JavaScript files, .hgplugin, run by QuickJS with
// the `HG` object described in PLUGINS.md and nothing else. The script that
// makes `HG` is shared with HorusGram for Android, so a plugin behaves the
// same on both.
namespace HoursGram::Plugins {

struct Command {
	QString name;
	QString description;
};

struct Plugin {
	QString id;
	QString name;
	QString version;
	QString author;
	QString description;
	QString source;
	QString error;
	QString settingsJson = u"[]"_q;
	std::vector<Command> commands;
	bool enabled = false;

	[[nodiscard]] bool hasSettings() const {
		return !settingsJson.isEmpty() && settingsJson != u"[]"_q;
	}
};

[[nodiscard]] bool IsPluginFile(const QString &fileName);

// Every installed plugin, in the order they were installed.
[[nodiscard]] std::vector<not_null<Plugin*>> List();
[[nodiscard]] rpl::producer<> Changes();

// Installs or replaces a plugin; its `error` is set when it fails to start.
// Returns nullptr when the text is not a plugin.
Plugin *Install(const QString &source);
Plugin *InstallExample();
void SetEnabled(not_null<Plugin*> plugin, bool enabled);
void Remove(not_null<Plugin*> plugin);

// Setting values as the JSON text of one value, empty when unset.
[[nodiscard]] QString SettingJson(not_null<Plugin*> plugin, const QString &key);
void SetSettingJson(
	not_null<Plugin*> plugin,
	const QString &key,
	const QString &json);

// The text to send in place of `text`, or nothing to send nothing.
[[nodiscard]] std::optional<QString> ProcessOutgoing(
	not_null<Main::Session*> session,
	PeerId peer,
	const QString &text);
void ProcessIncoming(not_null<HistoryItem*> item);

// What happens when a .hgplugin file is opened from a chat.
void AskInstall(
	not_null<Window::SessionController*> controller,
	const QString &source);

} // namespace HoursGram::Plugins
