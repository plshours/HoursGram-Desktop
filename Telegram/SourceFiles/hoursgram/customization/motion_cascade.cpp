/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/motion_cascade.h"

#include "base/call_delayed.h"
#include "ui/effects/animation_value.h"
#include "ui/ui_utility.h"

#include <QtCore/QPointer>

namespace HoursGram::Motion {
namespace {

constexpr auto kRowDuration = crl::time(360);
constexpr auto kRowStagger = crl::time(32);
constexpr auto kMaxRows = 24;
constexpr auto kStartFallback = crl::time(900);

} // namespace

CascadeReveal::CascadeReveal(
	not_null<QWidget*> parent,
	not_null<QWidget*> target,
	const style::color &bg,
	QPoint shift)
: RpWidget(parent)
, _target(target)
, _bg(bg)
, _shift(shift) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	_animation.init([=](crl::time now) {
		update();
		const auto rows = std::min(int(_rows.size()), kMaxRows);
		if (now - _started >= kRowDuration + rows * kRowStagger) {
			finish();
			return false;
		}
		return true;
	});
	const auto watch = [=] {
		setGeometry(_target->geometry());
	};
	watch();
	if (const auto rp = dynamic_cast<Ui::RpWidget*>(_target.get())) {
		rp->geometryValue() | rpl::on_next([=](QRect geometry) {
			setGeometry(geometry);
		}, lifetime());
	}
	raise();
	show();
	base::call_delayed(kStartFallback, this, [=] {
		start();
	});
}

void CascadeReveal::start() {
	if (_animation.animating()) {
		return;
	}
	if (_started) {
		return;
	}
	// A long column, like a profile with its shared media, is only drawn
	// as far as a window can show; what is below simply appears at the end.
	const auto limit = std::min(
		_target->height(),
		std::max(_target->window()->height(), 1));
	_snapshot = Ui::GrabWidget(_target, QRect(0, 0, _target->width(), limit));
	_rows.clear();
	for (const auto child : _target->children()) {
		const auto widget = qobject_cast<QWidget*>(child);
		if (widget
			&& !widget->isHidden()
			&& widget->height() > 0
			&& widget->y() < limit) {
			_rows.push_back(widget->geometry().intersected(
				QRect(0, 0, _target->width(), limit)));
		}
	}
	ranges::sort(_rows, [](const QRect &a, const QRect &b) {
		return a.y() < b.y();
	});
	if (_rows.empty() || _snapshot.isNull()) {
		finish();
		return;
	}
	_started = crl::now();
	_animation.start();
	update();
}

void CascadeReveal::finish() {
	_animation.stop();
	hide();
	deleteLater();
}

void CascadeReveal::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), _bg->b);
	if (_snapshot.isNull()) {
		return;
	}
	const auto now = crl::now();
	const auto ratio = _snapshot.devicePixelRatio();
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		const auto &row = _rows[i];
		const auto delay = std::min(i, kMaxRows) * kRowStagger;
		const auto progress = std::clamp(
			float64(now - _started - delay) / kRowDuration,
			0.,
			1.);
		if (progress <= 0.) {
			continue;
		}
		const auto eased = anim::easeOutCubic(1., progress);
		p.setOpacity(progress);
		const auto source = QRectF(
			row.x() * ratio,
			row.y() * ratio,
			row.width() * ratio,
			row.height() * ratio);
		const auto target = QRectF(
			row.x() + (1. - eased) * _shift.x(),
			row.y() + (1. - eased) * _shift.y(),
			row.width(),
			row.height());
		p.drawPixmap(target, _snapshot, source);
	}
}

Fn<void()> PrepareCascade(
		not_null<QWidget*> parent,
		not_null<QWidget*> target,
		const style::color &bg,
		QPoint shift) {
	if (anim::Disabled()) {
		return nullptr;
	}
	const auto cover = QPointer<CascadeReveal>(
		Ui::CreateChild<CascadeReveal>(parent.get(), target, bg, shift));
	return [=] {
		if (cover) {
			cover->start();
		}
	};
}

} // namespace HoursGram::Motion
