/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/graphite_frame.h"

#include "base/event_filter.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"

#include <QtGui/QPainterPath>
#include <QtGui/QRegion>
#include <QtGui/QtEvents>

namespace HoursGram::Customization {
namespace {

// Half of the gap between two panels: each panel gives up this much of its
// edge, so neighbours end up a full gap apart.
constexpr auto kHalfGap = 5;
constexpr auto kRadius = 24;
constexpr auto kBackdrop = 0x0E0E0E;

} // namespace

GraphiteFrame::GraphiteFrame(
	not_null<QWidget*> parent,
	Fn<std::vector<QRect>()> panels)
: RpWidget(parent)
, _panels(std::move(panels)) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);

	base::install_event_filter(this, parent, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ChildAdded && !isHidden()) {
			crl::on_main(this, [=] { raise(); });
		}
		return base::EventFilterResult::Continue;
	});

	refresh();
}

void GraphiteFrame::refresh() {
	const auto parent = parentWidget();
	const auto on = Ui::HoursGramGraphite();
	if (on && parent) {
		setGeometry(parent->rect());
		updateMask();
		raise();
	}
	setVisible(on);
	update();
}

void GraphiteFrame::updateMask() {
	const auto gap = style::ConvertScale(kHalfGap);
	const auto radius = style::ConvertScale(kRadius);
	auto mask = QRegion(rect());
	for (const auto &panel : _panels()) {
		const auto inner = panel.marginsRemoved({ gap, gap, gap, gap });
		if (inner.width() <= 0 || inner.height() <= 0) {
			continue;
		}
		const auto r = std::min(
			radius,
			std::min(inner.width(), inner.height()) / 2);
		mask -= QRegion(inner.marginsRemoved({ r, 0, r, 0 }));
		mask -= QRegion(inner.marginsRemoved({ 0, r, 0, r }));
	}
	if (mask.isEmpty()) {
		clearMask();
	} else {
		setMask(mask);
	}
}

void GraphiteFrame::paintEvent(QPaintEvent *e) {
	if (!Ui::HoursGramGraphite()) {
		return;
	}
	const auto gap = style::ConvertScale(kHalfGap);
	const auto radius = style::ConvertScale(kRadius);

	// The backdrop is everything but the rounded insides of the panels.
	auto path = QPainterPath();
	path.setFillRule(Qt::OddEvenFill);
	path.addRect(rect());
	for (const auto &panel : _panels()) {
		const auto inner = panel.marginsRemoved({ gap, gap, gap, gap });
		if (inner.width() <= 0 || inner.height() <= 0) {
			continue;
		}
		const auto r = std::min(
			radius,
			std::min(inner.width(), inner.height()) / 2);
		path.addRoundedRect(inner, r, r);
	}

	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setClipRect(e->rect());
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(
		(kBackdrop >> 16) & 0xFF,
		(kBackdrop >> 8) & 0xFF,
		kBackdrop & 0xFF));
	p.drawPath(path);
}

} // namespace HoursGram::Customization
