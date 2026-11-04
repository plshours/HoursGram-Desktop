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
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

extern "C" {
#include "quickjs.h"
}

namespace HoursGram::Plugins {
namespace {

// The same limits HorusGram for Android sets: a plugin gets two seconds for
// one call and a few dozen megabytes, then it is stopped.
constexpr auto kTimeLimit = crl::time(2000);
constexpr auto kMemoryLimit = size_t(32 * 1024 * 1024);
constexpr auto kStackLimit = size_t(1024 * 1024);
constexpr auto kMaxSource = qint64(1024 * 1024);

[[nodiscard]] QString Folder() {
	return cWorkingDir() + u"tdata/hoursgram_plugins/"_q;
}

[[nodiscard]] QString SourcePath(const QString &id) {
	return Folder() + id + u".hgplugin"_q;
}

[[nodiscard]] QString DataPath(const QString &id) {
	return Folder() + id + u".json"_q;
}

[[nodiscard]] QString IndexPath() {
	return Folder() + u"plugins.json"_q;
}

[[nodiscard]] QByteArray ReadAll(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void WriteAll(const QString &path, const QByteArray &bytes) {
	QDir().mkpath(Folder());
	auto file = QFile(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(bytes);
	}
}

struct Engine;

struct Entry {
	Plugin plugin;
	std::unique_ptr<Engine> engine;
	QJsonObject settings;
	QJsonObject storage;
};

struct State {
	std::vector<std::unique_ptr<Entry>> list;
	rpl::event_stream<> changes;
	bool loaded = false;
	int sendingFromPlugin = 0;
};

[[nodiscard]] State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] Entry *Find(const QString &id) {
	for (const auto &entry : Instance().list) {
		if (entry->plugin.id == id) {
			return entry.get();
		}
	}
	return nullptr;
}

[[nodiscard]] Entry *Find(not_null<Plugin*> plugin) {
	for (const auto &entry : Instance().list) {
		if (&entry->plugin == plugin.get()) {
			return entry.get();
		}
	}
	return nullptr;
}

void SaveIndex() {
	auto list = QJsonArray();
	for (const auto &entry : Instance().list) {
		list.push_back(QJsonObject{
			{ u"id"_q, entry->plugin.id },
			{ u"enabled"_q, entry->plugin.enabled },
		});
	}
	WriteAll(
		IndexPath(),
		QJsonDocument(list).toJson(QJsonDocument::Compact));
}

void SaveData(not_null<Entry*> entry) {
	WriteAll(
		DataPath(entry->plugin.id),
		QJsonDocument(QJsonObject{
			{ u"settings"_q, entry->settings },
			{ u"storage"_q, entry->storage },
		}).toJson(QJsonDocument::Compact));
}

// Values are kept as the JSON text the plugin runtime gives, so they come
// back exactly as they went in.
[[nodiscard]] QString ValueJson(const QJsonObject &object, const QString &key) {
	const auto i = object.find(key);
	return (i != object.end() && i->isString()) ? i->toString() : QString();
}

[[nodiscard]] Main::Session *ActiveSession() {
	if (!Core::IsAppLaunched()) {
		return nullptr;
	}
	const auto &account = Core::App().domain().active();
	return account.sessionExists() ? &account.session() : nullptr;
}

// Chat ids as HorusGram for Android writes them: users are positive, groups
// and channels negative.
[[nodiscard]] QString ChatIdString(PeerId peer) {
	if (peerIsUser(peer)) {
		return QString::number(peerToUser(peer).bare);
	} else if (peerIsChat(peer)) {
		return QString::number(-qint64(peerToChat(peer).bare));
	} else if (peerIsChannel(peer)) {
		return QString::number(-qint64(peerToChannel(peer).bare));
	}
	return u"0"_q;
}

[[nodiscard]] PeerId PeerFromChatId(
		not_null<Main::Session*> session,
		const QString &chatId) {
	auto ok = false;
	const auto value = chatId.toLongLong(&ok);
	if (!ok || !value) {
		return PeerId();
	} else if (value > 0) {
		return peerFromUser(UserId(BareId(value)));
	}
	const auto bare = BareId(-value);
	if (session->data().channelLoaded(ChannelId(bare))) {
		return peerFromChannel(ChannelId(bare));
	} else if (session->data().chatLoaded(ChatId(bare))) {
		return peerFromChat(ChatId(bare));
	}
	return PeerId();
}

void Toast(const QString &text) {
	if (!text.isEmpty()) {
		Ui::Toast::Show(text);
	}
}

struct Engine {
	explicit Engine(not_null<Entry*> entry) : entry(entry) {
	}
	~Engine() {
		if (context) {
			JS_FreeValue(context, runtimeObject);
			JS_FreeContext(context);
		}
		if (runtime) {
			JS_FreeRuntime(runtime);
		}
	}

	not_null<Entry*> entry;
	JSRuntime *runtime = nullptr;
	JSContext *context = nullptr;
	JSValue runtimeObject = JS_UNDEFINED;
	crl::time deadline = 0;
	bool busy = false;
};

int Interrupt(JSRuntime *runtime, void *opaque) {
	const auto engine = static_cast<Engine*>(opaque);
	return (engine->deadline && crl::now() > engine->deadline) ? 1 : 0;
}

[[nodiscard]] Engine *EngineOf(JSContext *context) {
	return static_cast<Engine*>(JS_GetContextOpaque(context));
}

[[nodiscard]] QString ArgString(JSContext *context, int argc, JSValueConst *argv, int index) {
	if (index >= argc) {
		return QString();
	}
	const auto value = JS_ToCString(context, argv[index]);
	if (!value) {
		return QString();
	}
	auto result = QString::fromUtf8(value);
	JS_FreeCString(context, value);
	return result;
}

[[nodiscard]] JSValue MakeString(JSContext *context, const QString &text) {
	const auto utf8 = text.toUtf8();
	return JS_NewStringLen(context, utf8.constData(), utf8.size());
}

[[nodiscard]] QString TakeException(JSContext *context) {
	const auto exception = JS_GetException(context);
	auto text = QString();
	if (const auto value = JS_ToCString(context, exception)) {
		text = QString::fromUtf8(value);
		JS_FreeCString(context, value);
	}
	if (JS_IsObject(exception)) {
		const auto stack = JS_GetPropertyStr(context, exception, "stack");
		if (!JS_IsUndefined(stack)) {
			if (const auto value = JS_ToCString(context, stack)) {
				const auto lines = QString::fromUtf8(value).trimmed();
				if (!lines.isEmpty()) {
					text += u"\n"_q + lines;
				}
				JS_FreeCString(context, value);
			}
		}
		JS_FreeValue(context, stack);
	}
	JS_FreeValue(context, exception);
	return text.isEmpty() ? u"error"_q : text;
}

JSValue BridgePlatform(JSContext *context, JSValueConst, int, JSValueConst*) {
	return JS_NewString(context, "desktop");
}

JSValue BridgeDeclareSettings(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
	const auto engine = EngineOf(context);
	const auto json = ArgString(context, argc, argv, 0);
	if (QJsonDocument::fromJson(json.toUtf8()).isArray()) {
		engine->entry->plugin.settingsJson = json;
	}
	return JS_UNDEFINED;
}

JSValue BridgeGetSetting(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
	const auto engine = EngineOf(context);
	const auto json = ValueJson(
		engine->entry->settings,
		ArgString(context, argc, argv, 0));
	return json.isEmpty() ? JS_NULL : MakeString(context, json);
}

JSValue BridgeStorageGet(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
	const auto engine = EngineOf(context);
	const auto json = ValueJson(
		engine->entry->storage,
		ArgString(context, argc, argv, 0));
	return json.isEmpty() ? JS_NULL : MakeString(context, json);
}

JSValue BridgeStorageSet(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
	const auto engine = EngineOf(context);
	const auto key = ArgString(context, argc, argv, 0);
	const auto json = ArgString(context, argc, argv, 1);
	if (!key.isEmpty()) {
		engine->entry->storage.insert(key, json);
		SaveData(engine->entry);
	}
	return JS_UNDEFINED;
}

JSValue BridgeSendMessage(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
	const auto chatId = ArgString(context, argc, argv, 0);
	const auto text = ArgString(context, argc, argv, 1);
	if (text.isEmpty()) {
		return JS_UNDEFINED;
	}
	// Sent once the plugin is done, so the send does not run inside it.
	crl::on_main([=] {
		const auto session = ActiveSession();
		if (!session) {
			return;
		}
		const auto peer = PeerFromChatId(session, chatId);
		if (!peer) {
			return;
		}
		auto message = Api::MessageToSend(
			Api::SendAction(session->data().history(peer)));
		message.textWithTags = { text };
		++Instance().sendingFromPlugin;
		session->api().sendMessage(std::move(message));
		--Instance().sendingFromPlugin;
	});
	return JS_UNDEFINED;
}

JSValue BridgeToast(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
	const auto text = ArgString(context, argc, argv, 0);
	crl::on_main([=] { Toast(text); });
	return JS_UNDEFINED;
}

JSValue BridgeLog(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
	const auto engine = EngineOf(context);
	LOG(("HorusGram plugin %1: %2"
		).arg(engine->entry->plugin.id
		).arg(ArgString(context, argc, argv, 0)));
	return JS_UNDEFINED;
}

void SetFunction(
		JSContext *context,
		JSValue object,
		const char *name,
		JSCFunction *function,
		int length) {
	JS_SetPropertyStr(
		context,
		object,
		name,
		JS_NewCFunction(context, function, name, length));
}

[[nodiscard]] QByteArray Prelude() {
	static const auto result = ReadAll(u":/hoursgram/plugins/prelude.js"_q);
	return result;
}

// Runs code in the plugin and hands back the error, if there was one.
[[nodiscard]] QString Eval(
		not_null<Engine*> engine,
		const QByteArray &code,
		const char *fileName) {
	engine->deadline = crl::now() + kTimeLimit;
	auto zero = code;
	zero.append('\0');
	const auto result = JS_Eval(
		engine->context,
		zero.constData(),
		code.size(),
		fileName,
		JS_EVAL_TYPE_GLOBAL);
	engine->deadline = 0;
	if (JS_IsException(result)) {
		return TakeException(engine->context);
	}
	JS_FreeValue(engine->context, result);
	return QString();
}

// Calls one method of the shared runtime; the answer is a JSON object.
[[nodiscard]] std::optional<QJsonObject> CallRuntime(
		not_null<Engine*> engine,
		const char *method,
		const QStringList &args,
		bool *wasArray = nullptr,
		QJsonArray *array = nullptr) {
	if (!engine->context || engine->busy) {
		return std::nullopt;
	}
	const auto context = engine->context;
	const auto function = JS_GetPropertyStr(
		context,
		engine->runtimeObject,
		method);
	if (!JS_IsFunction(context, function)) {
		JS_FreeValue(context, function);
		return std::nullopt;
	}
	auto values = std::vector<JSValue>();
	values.reserve(args.size());
	for (const auto &arg : args) {
		values.push_back(MakeString(context, arg));
	}
	engine->busy = true;
	engine->deadline = crl::now() + kTimeLimit;
	const auto result = JS_Call(
		context,
		function,
		engine->runtimeObject,
		int(values.size()),
		values.data());
	engine->deadline = 0;
	engine->busy = false;
	for (const auto &value : values) {
		JS_FreeValue(context, value);
	}
	JS_FreeValue(context, function);
	if (JS_IsException(result)) {
		LOG(("HorusGram plugin %1 failed in %2: %3"
			).arg(engine->entry->plugin.id
			).arg(QString::fromLatin1(method)
			).arg(TakeException(context)));
		return std::nullopt;
	}
	auto text = QByteArray();
	if (const auto value = JS_ToCString(context, result)) {
		text = QByteArray(value);
		JS_FreeCString(context, value);
	}
	JS_FreeValue(context, result);
	const auto document = QJsonDocument::fromJson(text);
	if (document.isArray() && array) {
		*array = document.array();
		if (wasArray) {
			*wasArray = true;
		}
		return QJsonObject();
	}
	if (!document.isObject()) {
		return std::nullopt;
	}
	return document.object();
}

void ReadCommands(not_null<Entry*> entry) {
	entry->plugin.commands.clear();
	if (!entry->engine) {
		return;
	}
	auto array = QJsonArray();
	auto wasArray = false;
	CallRuntime(entry->engine.get(), "commands", {}, &wasArray, &array);
	for (const auto &value : array) {
		const auto object = value.toObject();
		entry->plugin.commands.push_back({
			.name = object.value(u"name"_q).toString(),
			.description = object.value(u"description"_q).toString(),
		});
	}
}

void Stop(not_null<Entry*> entry) {
	entry->engine = nullptr;
	entry->plugin.commands.clear();
}

void Start(not_null<Entry*> entry) {
	Stop(entry);
	entry->plugin.error = QString();
	auto engine = std::make_unique<Engine>(entry);
	engine->runtime = JS_NewRuntime();
	if (!engine->runtime) {
		entry->plugin.error = u"no memory"_q;
		return;
	}
	JS_SetMemoryLimit(engine->runtime, kMemoryLimit);
	JS_SetMaxStackSize(engine->runtime, kStackLimit);
	JS_SetInterruptHandler(engine->runtime, Interrupt, engine.get());
	engine->context = JS_NewContext(engine->runtime);
	if (!engine->context) {
		entry->plugin.error = u"no memory"_q;
		return;
	}
	const auto context = engine->context;
	JS_SetContextOpaque(context, engine.get());

	const auto global = JS_GetGlobalObject(context);
	const auto bridge = JS_NewObject(context);
	SetFunction(context, bridge, "platform", BridgePlatform, 0);
	SetFunction(context, bridge, "declareSettings", BridgeDeclareSettings, 1);
	SetFunction(context, bridge, "getSetting", BridgeGetSetting, 1);
	SetFunction(context, bridge, "storageGet", BridgeStorageGet, 1);
	SetFunction(context, bridge, "storageSet", BridgeStorageSet, 2);
	SetFunction(context, bridge, "sendMessage", BridgeSendMessage, 2);
	SetFunction(context, bridge, "toast", BridgeToast, 1);
	SetFunction(context, bridge, "log", BridgeLog, 1);
	JS_SetPropertyStr(context, global, "__hgBridge", bridge);

	auto error = Eval(engine.get(), Prelude(), "horus_plugin_prelude.js");
	if (error.isEmpty()) {
		engine->runtimeObject = JS_GetPropertyStr(
			context,
			global,
			"__hgRuntime");
		// The plugin reaches the client only through `HG`.
		const auto hideBridge = JS_NewAtom(context, "__hgBridge");
		const auto hideRuntime = JS_NewAtom(context, "__hgRuntime");
		JS_DeleteProperty(context, global, hideBridge, 0);
		JS_DeleteProperty(context, global, hideRuntime, 0);
		JS_FreeAtom(context, hideBridge);
		JS_FreeAtom(context, hideRuntime);

		const auto name = (entry->plugin.id + u".hgplugin"_q).toUtf8();
		error = Eval(engine.get(), entry->plugin.source.toUtf8(), name.constData());
	}
	JS_FreeValue(context, global);
	if (!error.isEmpty()) {
		entry->plugin.error = error;
		LOG(("HorusGram plugin %1 did not start: %2"
			).arg(entry->plugin.id
			).arg(error));
		return;
	}
	entry->engine = std::move(engine);
	ReadCommands(entry);
}

struct Header {
	QString id;
	QString name;
	QString version;
	QString author;
	QString description;
};

[[nodiscard]] std::optional<Header> ParseHeader(const QString &source) {
	const auto start = source.indexOf(u"==HorusGramPlugin=="_q);
	const auto end = source.indexOf(u"==/HorusGramPlugin=="_q);
	if (start < 0 || end < start) {
		return std::nullopt;
	}
	static const auto line = QRegularExpression(
		u"^\\s*//\\s*@(\\w+)\\s+(.*?)\\s*$"_q,
		QRegularExpression::MultilineOption);
	auto result = Header();
	auto i = line.globalMatch(source.mid(start, end - start));
	while (i.hasNext()) {
		const auto match = i.next();
		const auto key = match.captured(1);
		const auto value = match.captured(2);
		if (key == u"id"_q) {
			result.id = value;
		} else if (key == u"name"_q) {
			result.name = value;
		} else if (key == u"version"_q) {
			result.version = value;
		} else if (key == u"author"_q) {
			result.author = value;
		} else if (key == u"description"_q) {
			result.description = value;
		}
	}
	static const auto valid = QRegularExpression(
		u"^[A-Za-z0-9._-]{1,64}$"_q);
	if (!valid.match(result.id).hasMatch() || result.name.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

void Apply(not_null<Entry*> entry, Header header, const QString &source) {
	entry->plugin.id = header.id;
	entry->plugin.name = header.name;
	entry->plugin.version = header.version;
	entry->plugin.author = header.author;
	entry->plugin.description = header.description;
	entry->plugin.source = source;
	entry->plugin.settingsJson = u"[]"_q;
}

void Load() {
	auto &state = Instance();
	if (state.loaded) {
		return;
	}
	state.loaded = true;
	const auto index = QJsonDocument::fromJson(ReadAll(IndexPath())).array();
	for (const auto &value : index) {
		const auto object = value.toObject();
		const auto id = object.value(u"id"_q).toString();
		const auto source = QString::fromUtf8(ReadAll(SourcePath(id)));
		const auto header = ParseHeader(source);
		if (!header || header->id != id || Find(id)) {
			continue;
		}
		auto entry = std::make_unique<Entry>();
		Apply(entry.get(), *header, source);
		entry->plugin.enabled = object.value(u"enabled"_q).toBool();
		const auto data = QJsonDocument::fromJson(ReadAll(DataPath(id)));
		entry->settings = data.object().value(u"settings"_q).toObject();
		entry->storage = data.object().value(u"storage"_q).toObject();
		const auto raw = entry.get();
		state.list.push_back(std::move(entry));
		if (raw->plugin.enabled) {
			Start(raw);
		}
	}
}

[[nodiscard]] std::vector<not_null<Entry*>> Running() {
	Load();
	auto result = std::vector<not_null<Entry*>>();
	for (const auto &entry : Instance().list) {
		if (entry->plugin.enabled && entry->engine) {
			result.push_back(entry.get());
		}
	}
	return result;
}

[[nodiscard]] QString Json(const QJsonObject &object) {
	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

bool IsPluginFile(const QString &fileName) {
	return fileName.endsWith(u".hgplugin"_q, Qt::CaseInsensitive);
}

std::vector<not_null<Plugin*>> List() {
	Load();
	auto result = std::vector<not_null<Plugin*>>();
	for (const auto &entry : Instance().list) {
		result.push_back(&entry->plugin);
	}
	return result;
}

rpl::producer<> Changes() {
	return Instance().changes.events();
}

Plugin *Install(const QString &source) {
	Load();
	const auto header = ParseHeader(source);
	if (!header || source.size() > kMaxSource) {
		return nullptr;
	}
	auto &state = Instance();
	auto entry = Find(header->id);
	if (!entry) {
		state.list.push_back(std::make_unique<Entry>());
		entry = state.list.back().get();
	}
	Stop(entry);
	Apply(entry, *header, source);
	entry->plugin.enabled = true;
	WriteAll(SourcePath(header->id), source.toUtf8());
	SaveIndex();
	Start(entry);
	state.changes.fire({});
	return &entry->plugin;
}

Plugin *InstallExample() {
	return Install(QString::fromUtf8(
		ReadAll(u":/hoursgram/plugins/example.hgplugin"_q)));
}

void SetEnabled(not_null<Plugin*> plugin, bool enabled) {
	const auto entry = Find(plugin);
	if (!entry || plugin->enabled == enabled) {
		return;
	}
	plugin->enabled = enabled;
	if (enabled) {
		Start(entry);
	} else {
		Stop(entry);
		plugin->error = QString();
	}
	SaveIndex();
	Instance().changes.fire({});
}

void Remove(not_null<Plugin*> plugin) {
	auto &list = Instance().list;
	const auto i = ranges::find_if(list, [&](const auto &entry) {
		return &entry->plugin == plugin.get();
	});
	if (i == end(list)) {
		return;
	}
	const auto id = plugin->id;
	list.erase(i);
	QFile::remove(SourcePath(id));
	QFile::remove(DataPath(id));
	SaveIndex();
	Instance().changes.fire({});
}

QString SettingJson(not_null<Plugin*> plugin, const QString &key) {
	const auto entry = Find(plugin);
	return entry ? ValueJson(entry->settings, key) : QString();
}

void SetSettingJson(
		not_null<Plugin*> plugin,
		const QString &key,
		const QString &json) {
	const auto entry = Find(plugin);
	if (!entry || key.isEmpty()) {
		return;
	}
	entry->settings.insert(key, json);
	SaveData(entry);
	Instance().changes.fire({});
}

std::optional<QString> ProcessOutgoing(
		not_null<Main::Session*> session,
		PeerId peer,
		const QString &text) {
	if (text.isEmpty() || Instance().sendingFromPlugin > 0) {
		return text;
	}
	const auto running = Running();
	if (running.empty()) {
		return text;
	}
	auto result = text;
	const auto chatId = ChatIdString(peer);
	const auto chat = Json({ { u"chatId"_q, chatId } });
	if (result.startsWith('.') && result.size() > 1) {
		const auto body = result.mid(1);
		const auto space = body.indexOf(' ');
		const auto name = (space < 0 ? body : body.mid(0, space)).toLower();
		const auto args = (space < 0) ? QString() : body.mid(space + 1).trimmed();
		for (const auto entry : running) {
			const auto has = ranges::any_of(
				entry->plugin.commands,
				[&](const Command &command) { return command.name == name; });
			if (!has) {
				continue;
			}
			const auto answer = CallRuntime(
				entry->engine.get(),
				"command",
				{ name, args, chat });
			if (!answer
				|| answer->value(u"cancel"_q).toBool()
				|| !answer->contains(u"text"_q)) {
				return std::nullopt;
			}
			result = answer->value(u"text"_q).toString();
			break;
		}
	}
	const auto fromId = QString::number(session->userId().bare);
	for (const auto entry : running) {
		const auto message = Json({
			{ u"text"_q, result },
			{ u"chatId"_q, chatId },
			{ u"fromId"_q, fromId },
			{ u"out"_q, true },
		});
		const auto answer = CallRuntime(
			entry->engine.get(),
			"dispatch",
			{ u"outgoingMessage"_q, message });
		if (!answer) {
			continue;
		} else if (answer->value(u"cancel"_q).toBool()) {
			return std::nullopt;
		} else if (answer->contains(u"text"_q)) {
			result = answer->value(u"text"_q).toString();
		}
	}
	return result;
}

void ProcessIncoming(not_null<HistoryItem*> item) {
	if (item->out()) {
		return;
	}
	const auto text = item->originalText().text;
	if (text.isEmpty()) {
		return;
	}
	const auto from = item->from();
	const auto message = Json({
		{ u"text"_q, text },
		{ u"chatId"_q, ChatIdString(item->history()->peer->id) },
		{ u"fromId"_q, ChatIdString(from->id) },
		{ u"out"_q, false },
	});
	// Seen after the message is in place, never in the middle of adding it.
	crl::on_main([=] {
		for (const auto entry : Running()) {
			CallRuntime(
				entry->engine.get(),
				"dispatch",
				{ u"incomingMessage"_q, message });
		}
	});
}

void AskInstall(
		not_null<Window::SessionController*> controller,
		const QString &source) {
	Load();
	const auto header = ParseHeader(source);
	if (!header) {
		controller->showToast(tr::hoursgram_PluginInvalid(tr::now));
		return;
	}
	const auto installed = (Find(header->id) != nullptr);
	auto text = header->name;
	if (!header->version.isEmpty()) {
		text += u" "_q + header->version;
	}
	if (!header->author.isEmpty()) {
		text += u"\n"_q + tr::hoursgram_PluginAuthor(
			tr::now,
			lt_name,
			header->author);
	}
	if (!header->description.isEmpty()) {
		text += u"\n\n"_q + header->description;
	}
	text += u"\n\n"_q + tr::hoursgram_PluginPermissions(tr::now);
	const auto weak = base::make_weak(controller);
	controller->show(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = [=](Fn<void()> close) {
			close();
			const auto plugin = Install(source);
			const auto strong = weak.get();
			if (!strong) {
				return;
			} else if (!plugin) {
				strong->showToast(tr::hoursgram_PluginInvalid(tr::now));
			} else if (!plugin->error.isEmpty()) {
				strong->showToast(tr::hoursgram_PluginFailed(
					tr::now,
					lt_error,
					plugin->error.split('\n').front()));
			} else {
				strong->showToast(tr::hoursgram_PluginInstalled(
					tr::now,
					lt_name,
					plugin->name));
			}
		},
		.confirmText = (installed
			? tr::hoursgram_PluginUpdate(tr::now)
			: tr::hoursgram_PluginInstall(tr::now)),
		.title = (installed
			? tr::hoursgram_PluginUpdate(tr::now)
			: tr::hoursgram_PluginInstall(tr::now)),
	}));
}

} // namespace HoursGram::Plugins
