/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace HoursGram::Customization {

// The floating panels of HORUSGRAM_DESIGN.md. Laid over a widget, it paints
// the dark backdrop in a band around each of the given panels and rounds
// their corners, so that they read as separate cards with gaps between
// them. It takes no input and draws only while Graphite is on; the
// geometry of what is under it does not change.
class GraphiteFrame final : public Ui::RpWidget {
public:
	// `panels` gives the panels in the coordinates of `parent`.
	GraphiteFrame(
		not_null<QWidget*> parent,
		Fn<std::vector<QRect>()> panels);

	// Keeps the frame over everything else in the parent and redraws it.
	void refresh();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Fn<std::vector<QRect>()> _panels;

};

} // namespace HoursGram::Customization
