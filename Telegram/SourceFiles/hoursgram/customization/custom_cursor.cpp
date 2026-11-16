/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/custom_cursor.h"

#include "ayu/ayu_settings.h"
#include "styles/style_basic.h"

#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace HoursGram::Customization {
namespace {

constexpr auto kGraphiteTheme = 4;
constexpr auto kSize = 20;

// The arrow is drawn rather than loaded, so it follows the accent of the
// palette and stays sharp at any scale of the screen.
[[nodiscard]] QCursor MakeArrow() {
	const auto ratio = std::max(qApp->devicePixelRatio(), 1.);
	auto image = QImage(
		QSize(kSize, kSize) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);

	auto path = QPainterPath();
	path.moveTo(1.5, 1.5);
	path.lineTo(1.5, 15.2);
	path.lineTo(5.1, 11.8);
	path.lineTo(7.7, 17.4);
	path.lineTo(10.3, 16.2);
	path.lineTo(7.8, 10.8);
	path.lineTo(12.8, 10.8);
	path.closeSubpath();

	auto p = QPainter(&image);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 80));
	p.drawPath(path.translated(0.6, 1.1));
	p.setPen(QPen(
		QColor(255, 255, 255),
		1.4,
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.setBrush(st::windowBgActive->c);
	p.drawPath(path);
	p.end();

	return QCursor(QPixmap::fromImage(std::move(image)), 1, 1);
}

// Qt draws the system arrow for every widget that has no cursor of its own
// or asks for Qt::ArrowCursor. While such a widget is under the mouse our
// arrow is put over it as the override cursor, and taken away as soon as
// the widget there wants a hand, a beam or anything else.
class Arrow final : public QObject {
public:
	Arrow() : QObject(qApp) {
		qApp->installEventFilter(this);
		schedule();
	}
	~Arrow() {
		// Deleted either when the theme changes or together with the
		// application, when there is nothing to remove the filter from.
		if (const auto app = QCoreApplication::instance()) {
			app->removeEventFilter(this);
			release();
		}
	}

	void refreshImage() {
		_cursor = MakeArrow();
		if (_applied && ours()) {
			QGuiApplication::changeOverrideCursor(_cursor);
		}
	}

protected:
	bool eventFilter(QObject *object, QEvent *e) override {
		switch (e->type()) {
		case QEvent::Enter:
		case QEvent::Leave:
		case QEvent::CursorChange:
		case QEvent::Show:
		case QEvent::Hide:
		case QEvent::ApplicationStateChange:
			schedule();
			break;
		default:
			break;
		}
		return false;
	}

private:
	void schedule() {
		if (_scheduled) {
			return;
		}
		_scheduled = true;
		QMetaObject::invokeMethod(this, [=] {
			_scheduled = false;
			check();
		}, Qt::QueuedConnection);
	}

	[[nodiscard]] bool ours() const {
		const auto current = QGuiApplication::overrideCursor();
		return current
			&& (current->shape() == Qt::BitmapCursor)
			&& (current->pixmap().cacheKey() == _cursor.pixmap().cacheKey());
	}

	void check() {
		const auto widget = QApplication::widgetAt(QCursor::pos());
		const auto arrow = widget
			&& (widget->cursor().shape() == Qt::ArrowCursor);
		if (arrow && !_applied) {
			QGuiApplication::setOverrideCursor(_cursor);
			_applied = true;
		} else if (!arrow && _applied) {
			release();
		}
	}

	void release() {
		// Someone else may have put a cursor over ours meanwhile, like the
		// wait cursor. Ours is taken away only from the top of the stack.
		if (_applied && ours()) {
			QGuiApplication::restoreOverrideCursor();
			_applied = false;
		}
	}

	QCursor _cursor = MakeArrow();
	bool _applied = false;
	bool _scheduled = false;

};

// Owned by the application, so it never outlives it.
QPointer<Arrow> &Instance() {
	static auto result = QPointer<Arrow>();
	return result;
}

void Toggle(bool enabled) {
	auto &instance = Instance();
	if (enabled && !instance) {
		instance = new Arrow();
	} else if (!enabled && instance) {
		delete instance.data();
	}
}

rpl::lifetime &Lifetime() {
	static auto result = rpl::lifetime();
	return result;
}

} // namespace

void StartCustomCursor() {
	auto &settings = AyuSettings::getInstance();
	Toggle(settings.clientTheme() == kGraphiteTheme);
	settings.clientThemeChanges() | rpl::on_next([](int theme) {
		Toggle(theme == kGraphiteTheme);
	}, Lifetime());
	style::PaletteChanged() | rpl::on_next([] {
		if (const auto instance = Instance().data()) {
			instance->refreshImage();
		}
	}, Lifetime());
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
		Toggle(false);
		Lifetime().destroy();
	});
}

} // namespace HoursGram::Customization
