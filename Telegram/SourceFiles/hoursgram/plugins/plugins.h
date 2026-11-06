/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

class History;
class HistoryItem;
class PeerData;
struct TextWithTags;

namespace Main {
class Session;
} // namespace Main

// HorusGram plugins: one JavaScript file with the extension .hgplugin, run
// by QuickJS with the `HG` object of PLUGINS.md and nothing else. The
// runtime that makes `HG` (Resources/hoursgram/horus_plugin_prelude.js) is
// the same file HorusGram for Android runs, so a plugin behaves the same
// on both.
namespace HoursGram::Plugins {

inline constexpr auto kExtension = ".hgplugin";

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
	QByteArray source;
	bool enabled = true;
	QString error;
	QJsonArray settings;
	std::vector<Command> commands;

	[[nodiscard]] bool running() const;

	struct Engine;
	std::unique_ptr<Engine> engine;

	Plugin();
	~Plugin();
};

// The header of a plugin, or nothing when the text is not one.
[[nodiscard]] std::unique_ptr<Plugin> Parse(const QByteArray &source);
[[nodiscard]] bool IsPluginFileName(const QString &name);

class Manager final {
public:
	[[nodiscard]] static Manager &Instance();

	[[nodiscard]] const std::vector<std::unique_ptr<Plugin>> &list();
	[[nodiscard]] Plugin *find(const QString &id);
	[[nodiscard]] rpl::producer<> changes() const;

	// Installs or replaces a plugin, keeping its settings and storage.
	// Returns nullptr when the text is not a plugin; otherwise the plugin,
	// with `error` set when its script failed to start.
	Plugin *install(const QByteArray &source);
	Plugin *installExample();
	void setEnabled(not_null<Plugin*> plugin, bool enabled);
	void remove(not_null<Plugin*> plugin);

	// Values are kept as JSON text, the way the bridge passes them.
	[[nodiscard]] QString setting(
		not_null<const Plugin*> plugin,
		const QString &key) const;
	void setSetting(
		not_null<Plugin*> plugin,
		const QString &key,
		const QString &json);
	[[nodiscard]] QString storageGet(
		not_null<const Plugin*> plugin,
		const QString &key) const;
	void storageSet(
		not_null<Plugin*> plugin,
		const QString &key,
		const QString &json);

	// False when the plugins decided the message is not to be sent;
	// otherwise `text` may have been changed by them.
	bool processOutgoing(not_null<PeerData*> peer, TextWithTags &text);
	void processIncoming(not_null<HistoryItem*> item);

	// A message a plugin sends itself is not handed to the plugins again.
	void sendFromPlugin(const QString &chatId, const QString &text);

private:
	Manager();

	void ensureLoaded();
	void start(not_null<Plugin*> plugin);
	void stop(not_null<Plugin*> plugin);
	[[nodiscard]] QByteArray callRaw(
		not_null<Plugin*> plugin,
		const char *method,
		const std::vector<QByteArray> &args);
	[[nodiscard]] QJsonObject call(
		not_null<Plugin*> plugin,
		const char *method,
		const std::vector<QByteArray> &args);

	[[nodiscard]] QJsonObject readData(const QString &id) const;
	void writeData(const QString &id, const QJsonObject &data) const;
	void writeState() const;

	std::vector<std::unique_ptr<Plugin>> _list;
	QByteArray _prelude;
	bool _loaded = false;
	bool _sendingFromPlugin = false;
	base::weak_ptr<Main::Session> _lastSession;
	rpl::event_stream<> _changes;

};

// Hooks for the rest of the client.
bool ProcessOutgoing(not_null<PeerData*> peer, TextWithTags &text);
void ProcessIncoming(not_null<HistoryItem*> item);

} // namespace HoursGram::Plugins
