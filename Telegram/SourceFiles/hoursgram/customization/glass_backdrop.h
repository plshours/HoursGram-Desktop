/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HoursGram::Customization {

// The media viewer is a translucent window of its own. With Graphite the
// client window under it is covered by a blurred picture of itself while
// the viewer is open, so the client shows through the viewer like through
// frosted glass instead of a flat gray veil.
void ShowGlassBackdrop(not_null<QWidget*> window);
void HideGlassBackdrop();

// The veil of the viewer: with Graphite a light tint over the glass,
// otherwise the color the theme gives.
[[nodiscard]] QColor ViewerBackground(QColor themed);

} // namespace HoursGram::Customization
