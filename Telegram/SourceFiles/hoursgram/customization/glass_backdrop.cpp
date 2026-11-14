/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/glass_backdrop.h"

#include "ui/abstract_button.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"

#include <QtCore/QPointer>
#include <QtGui/QPainter>

namespace HoursGram::Customization {
namespace {

// The picture is blurred at a quarter of its size, which is both much
// faster and softer: 7px there are 28px on the screen.
constexpr auto kDownscale = 4;
constexpr auto kBlurRadius = 7;
constexpr auto kSaturation = 1.35;

// The tint of the viewer over the glass, 42% of a near black.
constexpr auto kTintAlpha = 107;

[[nodiscard]] QPointer<Ui::AbstractButton> &Backdrop() {
	static auto result = QPointer<Ui::AbstractButton>();
	return result;
}

// Blurring takes the colors out of a picture, so they are made a little
// richer first, the way frosted glass looks.
void Saturate(QImage &image) {
	const auto clamp = [](float64 value) {
		return int(std::clamp(value, 0., 255.));
	};
	for (auto y = 0, height = image.height(); y != height; ++y) {
		const auto line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (auto x = 0, width = image.width(); x != width; ++x) {
			const auto color = line[x];
			const auto r = qRed(color);
			const auto g = qGreen(color);
			const auto b = qBlue(color);
			const auto gray = (r * 299 + g * 587 + b * 114) / 1000.;
			line[x] = qRgba(
				clamp(gray + (r - gray) * kSaturation),
				clamp(gray + (g - gray) * kSaturation),
				clamp(gray + (b - gray) * kSaturation),
				qAlpha(color));
		}
	}
}

[[nodiscard]] QImage PrepareGlass(QImage image) {
	const auto size = QSize(
		std::max(image.width() / kDownscale, 1),
		std::max(image.height() / kDownscale, 1));
	auto small = image.scaled(
		size,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	small.setDevicePixelRatio(1.);
	Saturate(small);
	return Images::BlurLargeImage(std::move(small), kBlurRadius);
}

} // namespace

void ShowGlassBackdrop(not_null<QWidget*> window) {
	HideGlassBackdrop();
	if (!Ui::HoursGramGraphite()
		|| window->isHidden()
		|| window->isMinimized()
		|| window->size().isEmpty()) {
		return;
	}
	auto image = Ui::GrabWidgetToImage(window);
	if (image.isNull()) {
		return;
	}
	const auto glass = PrepareGlass(std::move(image));

	// A button with no action, so a click on the covered window while the
	// viewer is open does not reach the chats under the glass.
	const auto cover = Ui::CreateChild<Ui::AbstractButton>(window.get());
	cover->setGeometry(window->rect());
	cover->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(cover);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(cover->rect(), glass);
	}, cover->lifetime());
	cover->show();
	cover->raise();
	Backdrop() = cover;
}

void HideGlassBackdrop() {
	if (const auto cover = Backdrop().data()) {
		delete cover;
	}
	Backdrop() = nullptr;
}

QColor ViewerBackground(QColor themed) {
	return Ui::HoursGramGraphite()
		? QColor(10, 10, 12, kTintAlpha)
		: themed;
}

} // namespace HoursGram::Customization
