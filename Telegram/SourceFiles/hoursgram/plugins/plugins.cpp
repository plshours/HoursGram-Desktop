/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/plugins/plugins.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/text/text_entity.h"
#include "ui/toast/toast.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>

#include "quickjs.h"

namespace HoursGram::Plugins {
namespace {

constexpr auto kMaxSize = 512 * 1024;
constexpr auto kTimeLimit = crl::time(2000);
constexpr auto kMemoryLimit = size_t(64 * 1024 * 1024);
constexpr auto kStackLimit = size_t(512 * 1024);

[[nodiscard]] QString Directory() {
	return cWorkingDir() + u"tdata/hoursgram_plugins/"_q;
}

[[nodiscard]] QString SourcePath(const QString &id) {
	return Directory() + id + QString::fromLatin1(kExtension);
}

[[nodiscard]] QString DataPath(const QString &id) {
	return Directory() + id + u".json"_q;
}

[[nodiscard]] QString StatePath() {
	return Directory() + u"plugins.json"_q;
}

[[nodiscard]] QByteArray ReadFile(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxSize) {
		return {};
	}
	return file.readAll();
}

bool WriteFile(const QString &path, const QByteArray &bytes) {
	QDir().mkpath(Directory());
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return (file.write(bytes) == bytes.size());
}

[[nodiscard]] QByteArray Compact(const QJsonObject &object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// Chats are named the way HorusGram for Android names them: a user by
// their id, a group or a channel by its id with a minus.
[[nodiscard]] QString DialogId(not_null<PeerData*> peer) {
	const auto id = peer->id;
	if (peerIsUser(id)) {
		return QString::number(peerToUser(id).bare);
	} else if (peerIsChat(id)) {
		return QString::number(-qint64(peerToChat(id).bare));
	}
	return QString::number(-qint64(peerToChannel(id).bare));
}

[[nodiscard]] PeerData *ResolveDialog(
		not_null<Main::Session*> session,
		const QString &dialogId) {
	auto ok = false;
	const auto value = dialogId.toLongLong(&ok);
	if (!ok || !value) {
		return nullptr;
	} else if (value > 0) {
		return session->data().peerLoaded(peerFromUser(UserId(value)));
	}
	const auto bare = BareId(-value);
	if (const auto channel = session->data().peerLoaded(
			peerFromChannel(ChannelId(bare)))) {
		return channel;
	}
	return session->data().peerLoaded(peerFromChat(ChatId(bare)));
}

} // namespace

// ---- The engine of one plugin.

struct Plugin::Engine {
	JSRuntime *runtime = nullptr;
	JSContext *context = nullptr;
	JSValue hg = JS_UNDEFINED;
	crl::time deadline = 0;

	~Engine() {
		if (context) {
			JS_FreeValue(context, hg);
			JS_FreeContext(context);
		}
		if (runtime) {
			JS_FreeRuntime(runtime);
		}
	}
};

Plugin::Plugin() = default;
Plugin::~Plugin() = default;

bool Plugin::running() const {
	return enabled && engine && engine->context;
}

namespace {

[[nodiscard]] QString ToQString(JSContext *ctx, JSValueConst value) {
	auto length = size_t();
	const auto data = JS_ToCStringLen(ctx, &length, value);
	if (!data) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return QString();
	}
	auto result = QString::fromUtf8(data, int(length));
	JS_FreeCString(ctx, data);
	return result;
}

[[nodiscard]] QString Arg(
		JSContext *ctx,
		int argc,
		JSValueConst *argv,
		int index) {
	return (index < argc) ? ToQString(ctx, argv[index]) : QString();
}

[[nodiscard]] Plugin *PluginOf(JSContext *ctx) {
	return static_cast<Plugin*>(JS_GetContextOpaque(ctx));
}

[[nodiscard]] JSValue NewString(JSContext *ctx, const QString &text) {
	const auto utf8 = text.toUtf8();
	return JS_NewStringLen(ctx, utf8.constData(), utf8.size());
}

[[nodiscard]] QString TakeException(JSContext *ctx) {
	const auto exception = JS_GetException(ctx);
	auto result = ToQString(ctx, exception);
	if (JS_IsError(ctx, exception)) {
		const auto stack = JS_GetPropertyStr(ctx, exception, "stack");
		if (JS_IsString(stack)) {
			const auto trace = ToQString(ctx, stack).trimmed();
			if (!trace.isEmpty()) {
				result += '\n' + trace;
			}
		}
		JS_FreeValue(ctx, stack);
	}
	JS_FreeValue(ctx, exception);
	return result.isEmpty() ? u"Unknown error"_q : result;
}

int Interrupt(JSRuntime *runtime, void *opaque) {
	const auto engine = static_cast<Plugin::Engine*>(opaque);
	return (engine->deadline && crl::now() > engine->deadline) ? 1 : 0;
}

void RunPendingJobs(not_null<Plugin::Engine*> engine) {
	auto ctx = (JSContext*)nullptr;
	while (true) {
		const auto result = JS_ExecutePendingJob(engine->runtime, &ctx);
		if (result == 0) {
			break;
		} else if (result < 0 && ctx) {
			LOG(("HorusGram plugin job failed: %1").arg(TakeException(ctx)));
		}
	}
}

// What a plugin reaches through `HG`; see horus_plugin_prelude.js. Every
// value crosses as a string, JSON when it is more than text.

JSValue BridgePlatform(
		JSContext *ctx,
		JSValueConst,
		int,
		JSValueConst*) {
	return JS_NewString(ctx, "desktop");
}

JSValue BridgeDeclareSettings(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	if (const auto plugin = PluginOf(ctx)) {
		plugin->settings = QJsonDocument::fromJson(
			Arg(ctx, argc, argv, 0).toUtf8()).array();
	}
	return JS_UNDEFINED;
}

JSValue BridgeGetSetting(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	const auto plugin = PluginOf(ctx);
	if (!plugin) {
		return JS_NULL;
	}
	const auto value = Manager::Instance().setting(
		plugin,
		Arg(ctx, argc, argv, 0));
	return value.isEmpty() ? JS_NULL : NewString(ctx, value);
}

JSValue BridgeStorageGet(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	const auto plugin = PluginOf(ctx);
	if (!plugin) {
		return JS_NULL;
	}
	const auto value = Manager::Instance().storageGet(
		plugin,
		Arg(ctx, argc, argv, 0));
	return value.isEmpty() ? JS_NULL : NewString(ctx, value);
}

JSValue BridgeStorageSet(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	if (const auto plugin = PluginOf(ctx)) {
		Manager::Instance().storageSet(
			plugin,
			Arg(ctx, argc, argv, 0),
			Arg(ctx, argc, argv, 1));
	}
	return JS_UNDEFINED;
}

// Sending and notices wait until the plugin is done, so that the client
// is never re-entered from inside a script.
JSValue BridgeSendMessage(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	const auto chatId = Arg(ctx, argc, argv, 0);
	const auto text = Arg(ctx, argc, argv, 1);
	if (!text.trimmed().isEmpty()) {
		crl::on_main([=] {
			Manager::Instance().sendFromPlugin(chatId, text);
		});
	}
	return JS_UNDEFINED;
}

JSValue BridgeToast(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	const auto text = Arg(ctx, argc, argv, 0);
	crl::on_main([=] {
		Ui::Toast::Show(text);
	});
	return JS_UNDEFINED;
}

JSValue BridgeLog(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	const auto plugin = PluginOf(ctx);
	LOG(("HorusGram plugin %1: %2").arg(
		plugin ? plugin->id : QString(),
		Arg(ctx, argc, argv, 0)));
	return JS_UNDEFINED;
}

void AddFunction(
		JSContext *ctx,
		JSValueConst object,
		const char *name,
		JSCFunction *function,
		int length) {
	JS_SetPropertyStr(
		ctx,
		object,
		name,
		JS_NewCFunction(ctx, function, name, length));
}

} // namespace

std::unique_ptr<Plugin> Parse(const QByteArray &source) {
	if (source.isEmpty() || source.size() > kMaxSize) {
		return nullptr;
	}
	static const auto header = QRegularExpression(
		uR"(//\s*==HorusGramPlugin==(.*?)//\s*==/HorusGramPlugin==)"_q,
		QRegularExpression::DotMatchesEverythingOption);
	static const auto field = QRegularExpression(
		uR"(^//\s*@([A-Za-z][A-Za-z-]*)\s+(.*)$)"_q);
	static const auto valid = QRegularExpression(
		uR"(^[A-Za-z0-9._-]{1,64}$)"_q);

	const auto text = QString::fromUtf8(source);
	const auto match = header.match(text);
	if (!match.hasMatch()) {
		return nullptr;
	}
	auto result = std::make_unique<Plugin>();
	result->source = source;
	const auto lines = match.captured(1).split(
		QRegularExpression(u"\r?\n"_q));
	for (const auto &line : lines) {
		const auto parts = field.match(line.trimmed());
		if (!parts.hasMatch()) {
			continue;
		}
		const auto key = parts.captured(1).toLower();
		const auto value = parts.captured(2).trimmed();
		if (key == u"id"_q) {
			result->id = value;
		} else if (key == u"name"_q) {
			result->name = value;
		} else if (key == u"version"_q) {
			result->version = value;
		} else if (key == u"author"_q) {
			result->author = value;
		} else if (key == u"description"_q) {
			result->description = value;
		}
	}
	if (!valid.match(result->id).hasMatch() || result->name.isEmpty()) {
		return nullptr;
	}
	return result;
}

bool IsPluginFileName(const QString &name) {
	return name.endsWith(
		QString::fromLatin1(kExtension),
		Qt::CaseInsensitive);
}

// ---- The list of plugins.

Manager::Manager() = default;

Manager &Manager::Instance() {
	static auto result = Manager();
	return result;
}

const std::vector<std::unique_ptr<Plugin>> &Manager::list() {
	ensureLoaded();
	return _list;
}

Plugin *Manager::find(const QString &id) {
	for (const auto &plugin : list()) {
		if (plugin->id == id) {
			return plugin.get();
		}
	}
	return nullptr;
}

rpl::producer<> Manager::changes() const {
	return _changes.events();
}

void Manager::ensureLoaded() {
	if (_loaded) {
		return;
	}
	_loaded = true;

	auto prelude = QFile(u":/gui/hoursgram/horus_plugin_prelude.js"_q);
	if (prelude.open(QIODevice::ReadOnly)) {
		_prelude = prelude.readAll();
	}

	const auto state = QJsonDocument::fromJson(
		ReadFile(StatePath())).object();
	const auto disabled = state.value(u"disabled"_q).toArray();

	const auto files = QDir(Directory()).entryList(
		{ u"*"_q + QString::fromLatin1(kExtension) },
		QDir::Files,
		QDir::Name);
	for (const auto &name : files) {
		auto plugin = Parse(ReadFile(Directory() + name));
		if (!plugin || find(plugin->id)) {
			continue;
		}
		plugin->enabled = !disabled.contains(QJsonValue(plugin->id));
		const auto raw = plugin.get();
		_list.push_back(std::move(plugin));
		if (raw->enabled) {
			start(raw);
		}
	}
}

void Manager::writeState() const {
	auto disabled = QJsonArray();
	for (const auto &plugin : _list) {
		if (!plugin->enabled) {
			disabled.push_back(plugin->id);
		}
	}
	WriteFile(StatePath(), Compact(QJsonObject{
		{ u"disabled"_q, disabled },
	}));
}

QJsonObject Manager::readData(const QString &id) const {
	return QJsonDocument::fromJson(ReadFile(DataPath(id))).object();
}

void Manager::writeData(const QString &id, const QJsonObject &data) const {
	WriteFile(DataPath(id), Compact(data));
}

Plugin *Manager::install(const QByteArray &source) {
	ensureLoaded();
	auto plugin = Parse(source);
	if (!plugin) {
		return nullptr;
	}
	for (auto i = _list.begin(); i != _list.end(); ++i) {
		if ((*i)->id == plugin->id) {
			stop(i->get());
			_list.erase(i);
			break;
		}
	}
	if (!WriteFile(SourcePath(plugin->id), source)) {
		return nullptr;
	}
	plugin->enabled = true;
	const auto raw = plugin.get();
	_list.push_back(std::move(plugin));
	writeState();
	start(raw);
	_changes.fire({});
	return raw;
}

Plugin *Manager::installExample() {
	auto file = QFile(u":/gui/hoursgram/horus_example.hgplugin"_q);
	return file.open(QIODevice::ReadOnly) ? install(file.readAll()) : nullptr;
}

void Manager::setEnabled(not_null<Plugin*> plugin, bool enabled) {
	if (plugin->enabled == enabled) {
		return;
	}
	plugin->enabled = enabled;
	if (enabled) {
		start(plugin);
	} else {
		stop(plugin);
	}
	writeState();
	_changes.fire({});
}

void Manager::remove(not_null<Plugin*> plugin) {
	stop(plugin);
	QFile::remove(SourcePath(plugin->id));
	QFile::remove(DataPath(plugin->id));
	for (auto i = _list.begin(); i != _list.end(); ++i) {
		if (i->get() == plugin) {
			_list.erase(i);
			break;
		}
	}
	writeState();
	_changes.fire({});
}

// Settings and storage live in one file per plugin, under "s" and "d",
// the same split HorusGram for Android makes.
QString Manager::setting(
		not_null<const Plugin*> plugin,
		const QString &key) const {
	return readData(plugin->id).value(u"s"_q).toObject().value(
		key).toString();
}

void Manager::setSetting(
		not_null<Plugin*> plugin,
		const QString &key,
		const QString &json) {
	auto data = readData(plugin->id);
	auto values = data.value(u"s"_q).toObject();
	values.insert(key, json);
	data.insert(u"s"_q, values);
	writeData(plugin->id, data);
}

QString Manager::storageGet(
		not_null<const Plugin*> plugin,
		const QString &key) const {
	return readData(plugin->id).value(u"d"_q).toObject().value(
		key).toString();
}

void Manager::storageSet(
		not_null<Plugin*> plugin,
		const QString &key,
		const QString &json) {
	auto data = readData(plugin->id);
	auto values = data.value(u"d"_q).toObject();
	values.insert(key, json);
	data.insert(u"d"_q, values);
	writeData(plugin->id, data);
}

// ---- Running.

void Manager::stop(not_null<Plugin*> plugin) {
	plugin->engine = nullptr;
	plugin->commands.clear();
}

void Manager::start(not_null<Plugin*> plugin) {
	stop(plugin);
	plugin->error = QString();
	plugin->settings = QJsonArray();
	if (_prelude.isEmpty()) {
		plugin->error = u"The plugin runtime is missing."_q;
		return;
	}

	plugin->engine = std::make_unique<Plugin::Engine>();
	const auto engine = plugin->engine.get();
	engine->runtime = JS_NewRuntime();
	if (!engine->runtime) {
		plugin->engine = nullptr;
		plugin->error = u"Not enough memory."_q;
		return;
	}
	JS_SetMemoryLimit(engine->runtime, kMemoryLimit);
	JS_SetMaxStackSize(engine->runtime, kStackLimit);
	JS_SetInterruptHandler(engine->runtime, Interrupt, engine);

	// JS_NewContext gives the standard objects only: no files, no network,
	// nothing of the client except the bridge below.
	const auto ctx = engine->context = JS_NewContext(engine->runtime);
	if (!ctx) {
		plugin->engine = nullptr;
		plugin->error = u"Not enough memory."_q;
		return;
	}
	JS_SetContextOpaque(ctx, plugin.get());

	const auto global = JS_GetGlobalObject(ctx);
	const auto bridge = JS_NewObject(ctx);
	AddFunction(ctx, bridge, "platform", BridgePlatform, 0);
	AddFunction(ctx, bridge, "declareSettings", BridgeDeclareSettings, 1);
	AddFunction(ctx, bridge, "getSetting", BridgeGetSetting, 1);
	AddFunction(ctx, bridge, "storageGet", BridgeStorageGet, 1);
	AddFunction(ctx, bridge, "storageSet", BridgeStorageSet, 2);
	AddFunction(ctx, bridge, "sendMessage", BridgeSendMessage, 2);
	AddFunction(ctx, bridge, "toast", BridgeToast, 1);
	AddFunction(ctx, bridge, "log", BridgeLog, 1);
	JS_SetPropertyStr(ctx, global, "__hgBridge", bridge);

	const auto evaluate = [&](const QByteArray &code, const char *name) {
		engine->deadline = crl::now() + kTimeLimit;
		const auto result = JS_Eval(
			ctx,
			code.constData(),
			size_t(code.size()),
			name,
			JS_EVAL_TYPE_GLOBAL);
		const auto failed = JS_IsException(result);
		JS_FreeValue(ctx, result);
		if (failed) {
			plugin->error = TakeException(ctx);
			return false;
		}
		RunPendingJobs(engine);
		return true;
	};

	auto ok = evaluate(_prelude, "horus_plugin_prelude.js");
	if (ok) {
		engine->hg = JS_GetPropertyStr(ctx, global, "__hgRuntime");
		// The plugin itself sees only `HG`.
		JS_SetPropertyStr(ctx, global, "__hgRuntime", JS_UNDEFINED);
		const auto atom = JS_NewAtom(ctx, "__hgBridge");
		JS_DeleteProperty(ctx, global, atom, 0);
		JS_FreeAtom(ctx, atom);

		const auto name = (plugin->id + QString::fromLatin1(kExtension));
		ok = evaluate(plugin->source, name.toUtf8().constData());
	}
	JS_FreeValue(ctx, global);
	if (!ok) {
		LOG(("HorusGram plugin %1 failed to start: %2").arg(
			plugin->id,
			plugin->error));
		plugin->engine = nullptr;
		return;
	}

	const auto commands = QJsonDocument::fromJson(
		callRaw(plugin, "commands", {})).array();
	for (const auto &entry : commands) {
		const auto command = entry.toObject();
		plugin->commands.push_back({
			.name = command.value(u"name"_q).toString(),
			.description = command.value(u"description"_q).toString(),
		});
	}
}

QByteArray Manager::callRaw(
		not_null<Plugin*> plugin,
		const char *method,
		const std::vector<QByteArray> &args) {
	if (!plugin->running()) {
		return QByteArray();
	}
	const auto engine = plugin->engine.get();
	const auto ctx = engine->context;
	engine->deadline = crl::now() + kTimeLimit;

	auto argv = std::vector<JSValue>();
	argv.reserve(args.size());
	for (const auto &arg : args) {
		argv.push_back(JS_NewStringLen(ctx, arg.constData(), arg.size()));
	}
	const auto function = JS_GetPropertyStr(ctx, engine->hg, method);
	const auto result = JS_Call(
		ctx,
		function,
		engine->hg,
		int(argv.size()),
		argv.data());
	JS_FreeValue(ctx, function);
	for (const auto &value : argv) {
		JS_FreeValue(ctx, value);
	}

	auto answer = QByteArray();
	if (JS_IsException(result)) {
		LOG(("HorusGram plugin %1 failed in %2: %3").arg(
			plugin->id,
			QString::fromLatin1(method),
			TakeException(ctx)));
	} else {
		answer = ToQString(ctx, result).toUtf8();
	}
	JS_FreeValue(ctx, result);
	RunPendingJobs(engine);
	engine->deadline = 0;
	return answer;
}

QJsonObject Manager::call(
		not_null<Plugin*> plugin,
		const char *method,
		const std::vector<QByteArray> &args) {
	return QJsonDocument::fromJson(callRaw(plugin, method, args)).object();
}

bool Manager::processOutgoing(
		not_null<PeerData*> peer,
		TextWithTags &text) {
	if (_sendingFromPlugin || text.text.isEmpty()) {
		return true;
	}
	auto running = std::vector<not_null<Plugin*>>();
	for (const auto &plugin : list()) {
		if (plugin->running()) {
			running.push_back(plugin.get());
		}
	}
	if (running.empty()) {
		return true;
	}
	_lastSession = &peer->session();

	const auto chatId = DialogId(peer);
	const auto chat = Compact(QJsonObject{ { u"chatId"_q, chatId } });
	auto value = text.text;

	// `.name args` runs a command of the first plugin that has it.
	if (value.size() > 1 && value[0] == QChar('.')) {
		const auto body = value.mid(1);
		auto end = 0;
		while (end < body.size() && !body[end].isSpace()) {
			++end;
		}
		const auto name = body.left(end).toLower();
		const auto args = body.mid(end).trimmed();
		for (const auto plugin : running) {
			const auto has = ranges::any_of(
				plugin->commands,
				[&](const Command &command) { return command.name == name; });
			if (!has) {
				continue;
			}
			const auto result = call(
				plugin,
				"command",
				{ name.toUtf8(), args.toUtf8(), chat });
			if (result.value(u"cancel"_q).toBool()
				|| !result.contains(u"text"_q)) {
				return false;
			}
			value = result.value(u"text"_q).toString();
			break;
		}
	}

	const auto fromId = QString::number(peer->session().userId().bare);
	for (const auto plugin : running) {
		const auto message = Compact(QJsonObject{
			{ u"text"_q, value },
			{ u"chatId"_q, chatId },
			{ u"fromId"_q, fromId },
			{ u"out"_q, true },
		});
		const auto result = call(
			plugin,
			"dispatch",
			{ QByteArray("outgoingMessage"), message });
		if (result.value(u"cancel"_q).toBool()) {
			return false;
		} else if (result.contains(u"text"_q)) {
			value = result.value(u"text"_q).toString();
		}
	}

	if (value != text.text) {
		// The formatting belonged to the old text.
		text = TextWithTags{ value };
	}
	return !value.trimmed().isEmpty();
}

void Manager::processIncoming(not_null<HistoryItem*> item) {
	if (item->out() || item->isLocal()) {
		return;
	}
	const auto &text = item->originalText().text;
	if (text.isEmpty()) {
		return;
	}
	const auto any = ranges::any_of(list(), [](const auto &plugin) {
		return plugin->running();
	});
	if (!any) {
		return;
	}
	_lastSession = &item->history()->session();
	const auto message = Compact(QJsonObject{
		{ u"text"_q, text },
		{ u"chatId"_q, DialogId(item->history()->peer) },
		{ u"fromId"_q, DialogId(item->from()) },
		{ u"out"_q, false },
	});
	// Off the stack that is adding the message to the history.
	crl::on_main([=] {
		auto &manager = Instance();
		for (const auto &plugin : manager.list()) {
			if (plugin->running()) {
				manager.call(
					plugin.get(),
					"dispatch",
					{ QByteArray("incomingMessage"), message });
			}
		}
	});
}

void Manager::sendFromPlugin(const QString &chatId, const QString &text) {
	auto session = _lastSession.get();
	if (!session) {
		auto &account = Core::App().domain().active();
		if (!account.sessionExists()) {
			return;
		}
		session = &account.session();
	}
	const auto peer = ResolveDialog(session, chatId);
	if (!peer) {
		LOG(("HorusGram plugin: unknown chat %1").arg(chatId));
		return;
	}
	auto action = Api::SendAction(session->data().history(peer));
	action.clearDraft = false;
	auto message = Api::MessageToSend(action);
	message.textWithTags = TextWithTags{ text };
	_sendingFromPlugin = true;
	session->api().sendMessage(std::move(message));
	_sendingFromPlugin = false;
}

bool ProcessOutgoing(not_null<PeerData*> peer, TextWithTags &text) {
	return Manager::Instance().processOutgoing(peer, text);
}

void ProcessIncoming(not_null<HistoryItem*> item) {
	Manager::Instance().processIncoming(item);
}

} // namespace HoursGram::Plugins
