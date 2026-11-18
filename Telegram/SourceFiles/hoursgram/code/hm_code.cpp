/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/code/hm_code.h"

#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/rp_window.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "window/window_session_controller.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace HoursGram::Code {
namespace {

// The assistant lives only here: the key the user pasted goes to this host
// and nowhere else, and only these two paths of its API are passed on.
constexpr auto kManulBase = "https://api.manulchiki.shop/v1";
constexpr auto kManulTimeout = 180 * 1000;
constexpr auto kMaxFileSize = qint64(16 * 1024 * 1024);
constexpr auto kPage = "hmcode/index.html";

class Editor final : public base::has_weak_ptr {
public:
	explicit Editor(not_null<Window::SessionController*> controller);
	~Editor();

	[[nodiscard]] Window::SessionController *controller() const {
		return _controller;
	}
	void activate();

	[[nodiscard]] rpl::producer<> closeRequests() const {
		return _closeRequests.events();
	}
	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void createWindow();
	void createWebview();
	void showError(TextWithEntities text);
	void handle(const QJsonObject &message);
	void reply(int id, QJsonObject result);
	void fail(int id, const QString &error);
	void manul(int id, const QJsonObject &message);
	void pickFolder(int id);
	void fileOperation(int id, const QString &op, const QJsonObject &message);
	[[nodiscard]] QString resolve(const QString &relative) const;

	const not_null<Window::SessionController*> _controller;
	std::unique_ptr<Ui::RpWindow> _window;
	Ui::RpWidget *_container = nullptr;
	std::unique_ptr<Webview::Window> _webview;
	std::unique_ptr<QNetworkAccessManager> _network;
	QString _root;
	bool _dying = false;
	rpl::event_stream<> _closeRequests;
	rpl::lifetime _lifetime;

};

Editor::Editor(not_null<Window::SessionController*> controller)
: _controller(controller) {
	createWindow();
	createWebview();
}

Editor::~Editor() {
	_dying = true;
	_network = nullptr;
	_webview = nullptr;
}

void Editor::createWindow() {
	_window = std::make_unique<Ui::RpWindow>();
	const auto window = _window.get();
	window->setTitle(u"HM Code"_q);
	window->setMinimumSize(QSize(
		style::ConvertScale(900),
		style::ConvertScale(600)));
	window->resize(
		style::ConvertScale(1320),
		style::ConvertScale(840));

	_container = Ui::CreateChild<Ui::RpWidget>(window->body().get());
	window->body()->sizeValue() | rpl::on_next([=](QSize size) {
		_container->setGeometry(QRect(QPoint(), size));
	}, _container->lifetime());
	_container->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, QColor(0x12, 0x12, 0x12));
	}, _container->lifetime());
	_container->show();

	window->events() | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::Close);
	}) | rpl::on_next([=] {
		_closeRequests.fire({});
	}, window->lifetime());
}

void Editor::createWebview() {
	_webview = std::make_unique<Webview::Window>(
		_container,
		Webview::WindowConfig{
			.opaqueBg = QColor(0x12, 0x12, 0x12),
			.storageId = _controller->session().local().resolveStorageIdOther(),
			.safe = true,
		});
	const auto raw = _webview.get();
	const auto widget = raw->widget();
	if (!widget) {
		_webview = nullptr;
		const auto available = Webview::Availability();
		showError((available.error != Webview::Available::Error::None)
			? Ui::BotWebView::ErrorText(available)
			: TextWithEntities{ u"Error: Could not initialize WebView."_q });
		return;
	}
	widget->show();
	_container->sizeValue() | rpl::on_next([=](QSize size) {
		if (const auto widget = raw->widget()) {
			widget->setGeometry(QRect(QPoint(), size));
		}
	}, _container->lifetime());

	// The page itself stays in place; any link it opens goes to the
	// usual browser of the system.
	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		if (!newWindow
			&& (uri.contains(u"/hmcode/"_q) || uri == u"about:blank"_q)) {
			return true;
		}
		const auto scheme = QUrl(uri).scheme().toLower();
		if (scheme == u"https"_q || scheme == u"http"_q || scheme == u"tg"_q) {
			File::OpenUrl(uri);
		}
		return false;
	});
	raw->setNavigationDoneHandler([=](bool success) {
	});
	raw->setMessageHandler([=](const QJsonDocument &message) {
		const auto object = message.object();
		crl::on_main(_container, [=] {
			handle(object);
		});
	});
	raw->setDataRequestHandler([=](Webview::DataRequest request) {
		auto id = request.id;
		if (const auto pos = id.find_first_of("#?"); pos != id.npos) {
			id = id.substr(0, pos);
		}
		if (id != kPage) {
			return Webview::DataResult::Failed;
		}
		auto file = QFile(u":/hmcode/index.html"_q);
		if (!file.open(QIODevice::ReadOnly)) {
			return Webview::DataResult::Failed;
		}
		request.done({
			.stream = std::make_unique<Webview::DataStreamFromMemory>(
				file.readAll(),
				"text/html; charset=utf-8"),
		});
		return Webview::DataResult::Done;
	});
	raw->init(R"()");
	raw->navigateToData(kPage);
}

void Editor::showError(TextWithEntities text) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		_container,
		rpl::single(std::move(text)),
		st::defaultFlatLabel);
	label->setTextColorOverride(QColor(0xB8, 0xB8, 0xB8));
	_container->sizeValue() | rpl::on_next([=](QSize size) {
		const auto width = std::min(size.width() - 40, 480);
		label->resizeToWidth(std::max(width, 100));
		label->moveToLeft(
			(size.width() - label->width()) / 2,
			(size.height() - label->height()) / 2);
	}, label->lifetime());
	label->show();
}

void Editor::activate() {
	if (_window->isMinimized()) {
		_window->showNormal();
	} else if (_window->isHidden()) {
		_window->show();
	}
	_window->raise();
	_window->activateWindow();
	if (_webview) {
		_webview->focus();
	}
}

void Editor::reply(int id, QJsonObject result) {
	if (_dying || !_webview) {
		return;
	}
	const auto json = QJsonDocument(
		QJsonArray{ id, result }).toJson(QJsonDocument::Compact);
	_webview->eval(
		"window.hmNative && window.hmNative.apply(null, " + json + ");");
}

void Editor::fail(int id, const QString &error) {
	reply(id, QJsonObject{ { u"error"_q, error } });
}

void Editor::handle(const QJsonObject &message) {
	if (message.value(u"event"_q).toString() != u"native"_q) {
		return;
	}
	const auto id = message.value(u"id"_q).toInt();
	const auto op = message.value(u"op"_q).toString();
	if (op == u"manul"_q) {
		manul(id, message);
	} else if (op == u"copy"_q) {
		QGuiApplication::clipboard()->setText(
			message.value(u"text"_q).toString());
		reply(id, {});
	} else if (op == u"pickFolder"_q) {
		pickFolder(id);
	} else {
		fileOperation(id, op, message);
	}
}

void Editor::manul(int id, const QJsonObject &message) {
	const auto path = message.value(u"path"_q).toString();
	if (path != u"/models"_q && path != u"/chat/completions"_q) {
		fail(id, u"Неизвестный запрос"_q);
		return;
	}
	const auto key = message.value(u"key"_q).toString().trimmed();
	if (key.isEmpty()
		|| key.size() > 512
		|| key.contains('\n')
		|| key.contains('\r')) {
		fail(id, u"Неверный ключ"_q);
		return;
	}
	if (!_network) {
		_network = std::make_unique<QNetworkAccessManager>();
	}
	auto request = QNetworkRequest(
		QUrl(QString::fromLatin1(kManulBase) + path));
	request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
	request.setRawHeader("Accept", "application/json");
	request.setTransferTimeout(kManulTimeout);
	const auto body = message.value(u"body"_q).toString().toUtf8();
	auto sent = (QNetworkReply*)nullptr;
	if (body.isEmpty()) {
		sent = _network->get(request);
	} else {
		request.setHeader(
			QNetworkRequest::ContentTypeHeader,
			u"application/json"_q);
		sent = _network->post(request, body);
	}
	const auto weak = base::make_weak(this);
	QObject::connect(sent, &QNetworkReply::finished, [=] {
		sent->deleteLater();
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto status = sent->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto text = status
			? QString::fromUtf8(sent->readAll())
			: sent->errorString();
		strong->reply(id, QJsonObject{
			{ u"status"_q, status },
			{ u"text"_q, text },
		});
	});
}

void Editor::pickFolder(int id) {
	const auto weak = base::make_weak(this);
	FileDialog::GetFolder(
		_window.get(),
		u"HM Code"_q,
		_root,
		[=](QString &&path) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			const auto root = QDir::cleanPath(
				QFileInfo(path).absoluteFilePath());
			if (root.isEmpty() || !QFileInfo(root).isDir()) {
				strong->reply(id, {});
				return;
			}
			strong->_root = root;
			const auto name = QFileInfo(root).fileName();
			strong->reply(id, QJsonObject{
				{ u"name"_q, name.isEmpty() ? root : name },
			});
		},
		[=] {
			if (const auto strong = weak.get()) {
				strong->reply(id, {});
			}
		});
}

// Every path the page names is inside the folder the user picked: the
// page can neither leave it with ".." nor reach anything else on disk.
QString Editor::resolve(const QString &relative) const {
	if (_root.isEmpty()) {
		return QString();
	}
	const auto full = QDir::cleanPath(_root + '/' + relative);
	return (full == _root || full.startsWith(_root + '/'))
		? full
		: QString();
}

void Editor::fileOperation(
		int id,
		const QString &op,
		const QJsonObject &message) {
	if (op == u"move"_q) {
		const auto from = resolve(message.value(u"from"_q).toString());
		const auto to = resolve(message.value(u"to"_q).toString());
		if (from.isEmpty() || to.isEmpty() || from == _root || to == _root) {
			fail(id, u"Нет доступа к этому пути"_q);
		} else if (QFileInfo::exists(to)) {
			fail(id, u"Там уже есть файл с таким именем"_q);
		} else if (!QDir().rename(from, to)) {
			fail(id, u"Не удалось переместить"_q);
		} else {
			reply(id, {});
		}
		return;
	}
	const auto path = resolve(message.value(u"path"_q).toString());
	if (path.isEmpty()) {
		fail(id, u"Нет доступа к этому пути"_q);
		return;
	}
	const auto info = QFileInfo(path);
	if (op == u"list"_q) {
		auto entries = QJsonArray();
		const auto list = QDir(path).entryInfoList(
			QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
			QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
		for (const auto &entry : list) {
			entries.push_back(QJsonObject{
				{ u"name"_q, entry.fileName() },
				{ u"dir"_q, entry.isDir() },
			});
		}
		reply(id, QJsonObject{ { u"entries"_q, entries } });
	} else if (op == u"read"_q) {
		if (info.size() > kMaxFileSize) {
			fail(id, u"Файл слишком большой"_q);
			return;
		}
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			fail(id, u"Не удалось прочитать файл"_q);
			return;
		}
		reply(id, QJsonObject{
			{ u"data"_q, QString::fromLatin1(file.readAll().toBase64()) },
		});
	} else if (op == u"write"_q) {
		const auto bytes = QByteArray::fromBase64(
			message.value(u"data"_q).toString().toLatin1());
		if (bytes.size() > kMaxFileSize) {
			fail(id, u"Файл слишком большой"_q);
			return;
		}
		auto file = QSaveFile(path);
		if (!file.open(QIODevice::WriteOnly)
			|| file.write(bytes) != bytes.size()
			|| !file.commit()) {
			fail(id, u"Не удалось сохранить файл"_q);
			return;
		}
		reply(id, {});
	} else if (op == u"touch"_q) {
		if (!info.exists()) {
			QDir().mkpath(info.absolutePath());
			auto file = QFile(path);
			if (!file.open(QIODevice::WriteOnly)) {
				fail(id, u"Не удалось создать файл"_q);
				return;
			}
		}
		reply(id, {});
	} else if (op == u"mkdir"_q) {
		if (!QDir().mkpath(path)) {
			fail(id, u"Не удалось создать папку"_q);
			return;
		}
		reply(id, {});
	} else if (op == u"remove"_q) {
		if (path == _root) {
			fail(id, u"Нельзя удалить саму папку проекта"_q);
			return;
		}
		const auto removed = info.isDir()
			? QDir(path).removeRecursively()
			: QFile::remove(path);
		if (!removed) {
			fail(id, u"Не удалось удалить"_q);
			return;
		}
		reply(id, {});
	} else {
		fail(id, u"Неизвестная операция"_q);
	}
}

std::unique_ptr<Editor> Instance;

} // namespace

void Show(not_null<Window::SessionController*> controller) {
	if (Instance && Instance->controller() != controller) {
		Instance = nullptr;
	}
	if (!Instance) {
		Instance = std::make_unique<Editor>(controller);
		const auto raw = Instance.get();
		raw->closeRequests() | rpl::on_next([=] {
			crl::on_main([=] {
				if (Instance.get() == raw) {
					Instance = nullptr;
				}
			});
		}, raw->lifetime());
		controller->lifetime().add([=] {
			if (Instance.get() == raw) {
				Instance = nullptr;
			}
		});
	}
	Instance->activate();
}

} // namespace HoursGram::Code
