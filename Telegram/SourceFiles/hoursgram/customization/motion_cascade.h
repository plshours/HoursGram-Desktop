/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace HoursGram::Motion {

// Lies over a column of rows and hides them behind the background until
// start() is called. Then the rows come in one after another, each moving
// from `shift` to its place and fading in, drawn from a snapshot of the
// column, and the cover removes itself. It lets the mouse through, so the
// real rows under it can be used while the cascade still runs. Should the
// start never come, it starts by itself a moment later, so nothing stays
// hidden.
class CascadeReveal final : public Ui::RpWidget {
public:
	CascadeReveal(
		not_null<QWidget*> parent,
		not_null<QWidget*> target,
		const style::color &bg,
		QPoint shift);

	void start();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void finish();

	const not_null<QWidget*> _target;
	const style::color &_bg;
	const QPoint _shift;
	QPixmap _snapshot;
	std::vector<QRect> _rows;
	Ui::Animations::Basic _animation;
	crl::time _started = 0;

};

// Covers `target` and returns what starts the cascade. Returns nothing
// when animations are off, so the rows are simply there.
[[nodiscard]] Fn<void()> PrepareCascade(
	not_null<QWidget*> parent,
	not_null<QWidget*> target,
	const style::color &bg,
	QPoint shift);

} // namespace HoursGram::Motion
