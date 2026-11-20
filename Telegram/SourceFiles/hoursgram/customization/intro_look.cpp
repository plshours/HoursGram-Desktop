/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/intro_look.h"

#include "ayu/ayu_settings.h"
#include "base/timer.h"
#include "hoursgram/music/music_common.h"
#include "lang/lang_keys.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_hoursgram_design.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>

#include <cmath>

namespace HoursGram::Customization {
namespace {

using Background = AyuSettings::IntroBackground;
using HoursGram::Music::Tr;

// The glow is soft, so it is drawn this many times smaller and scaled up:
// that keeps a full window of gradients cheap enough to move.
constexpr auto kGlowScale = 6;

// The preview draws the screen of this size, scaled down to fit.
constexpr auto kPreviewWidth = 1000;
constexpr auto kPreviewHeight = 640;

[[nodiscard]] QColor Parse(const QString &hex, QColor fallback) {
	if (hex.isEmpty()) {
		return fallback;
	}
	const auto color = QColor(u"#"_q + hex);
	return color.isValid() ? color : fallback;
}

[[nodiscard]] QColor AutoFirstColor() {
	return anim::color(st::windowBg, st::windowBgActive, 0.45);
}

[[nodiscard]] QColor AutoSecondColor() {
	return anim::color(st::windowBg, st::windowBgActive, 0.06);
}

[[nodiscard]] QColor FirstColor() {
	return Parse(AyuSettings::getInstance().introColor1(), AutoFirstColor());
}

[[nodiscard]] QColor SecondColor() {
	return Parse(AyuSettings::getInstance().introColor2(), AutoSecondColor());
}

void PaintGradient(QPainter &p, QRect rect) {
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., FirstColor());
	gradient.setColorAt(1., SecondColor());
	p.fillRect(rect, gradient);
}

void PaintGlow(QPainter &p, QRect rect, crl::time now) {
	const auto first = FirstColor();
	const auto second = SecondColor();
	const auto small = QSize(
		std::max(rect.width() / kGlowScale, 1),
		std::max(rect.height() / kGlowScale, 1));
	auto image = QImage(small, QImage::Format_ARGB32_Premultiplied);
	image.fill(second);
	{
		auto q = QPainter(&image);
		auto hq = PainterHighQualityEnabler(q);
		const auto t = now / 1000.;
		const auto w = float64(small.width());
		const auto h = float64(small.height());
		const auto size = std::max(w, h);
		const auto blob = [&](QPointF center, float64 radius, QColor color) {
			auto gradient = QRadialGradient(center, radius);
			auto transparent = color;
			transparent.setAlpha(0);
			gradient.setColorAt(0., color);
			gradient.setColorAt(1., transparent);
			q.fillRect(image.rect(), gradient);
		};
		// Three soft lights drift slowly along their own paths, so the
		// picture never repeats in an obvious way.
		auto main = first;
		main.setAlpha(235);
		blob(
			{
				w * (0.25 + 0.12 * std::sin(t * 0.21)),
				h * (0.30 + 0.15 * std::cos(t * 0.17)),
			},
			size * 0.62,
			main);
		// A white accent (HorusGram WINDOW) would glare at full strength.
		auto accent = st::windowBgActive->c;
		accent.setAlpha(accent.lightness() > 200 ? 56 : 120);
		blob(
			{
				w * (0.78 + 0.10 * std::cos(t * 0.13)),
				h * (0.72 + 0.12 * std::sin(t * 0.19)),
			},
			size * 0.55,
			accent);
		auto mixed = anim::color(first, accent, 0.5);
		mixed.setAlpha(170);
		blob(
			{
				w * (0.58 + 0.20 * std::sin(t * 0.09 + 1.)),
				h * (0.12 + 0.10 * std::cos(t * 0.11 + 2.)),
			},
			size * 0.45,
			mixed);
	}
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(rect, image);
	p.restore();
}

void PaintCard(QPainter &p, QRect card) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = float64(st::introLookCardRadius);

	// A soft shadow under the card, a little lower than the card itself.
	const auto shadow = st::introLookCardShadow;
	const auto steps = 6;
	p.setPen(Qt::NoPen);
	for (auto i = steps; i > 0; --i) {
		const auto grow = shadow * i / steps;
		auto color = st::windowShadowFg->c;
		color.setAlphaF(0.045);
		p.setBrush(color);
		p.drawRoundedRect(
			QRectF(card).marginsAdded(
				{ grow * 1., grow * 1., grow * 1., grow * 1. }
			).translated(0, shadow / 4.),
			radius + grow,
			radius + grow);
	}

	p.setBrush(st::windowBg);
	p.setPen(QPen(st::windowBgOver->c, st::lineWidth));
	const auto half = st::lineWidth / 2.;
	p.drawRoundedRect(
		QRectF(card).marginsRemoved({ half, half, half, half }),
		radius,
		radius);
}

[[nodiscard]] QColor TitleColor() {
	if (AyuSettings::getInstance().introBackground() == Background::Plain) {
		return st::introTitleFg->c;
	}
	const auto middle = anim::color(FirstColor(), SecondColor(), 0.5);
	return (middle.lightness() < 150)
		? QColor(255, 255, 255)
		: QColor(0x1F, 0x1F, 0x24);
}

void PaintTitle(QPainter &p, QRect card, const QString &title) {
	if (title.isEmpty()) {
		return;
	}
	const auto &font = st::introLookTitleFont;
	const auto top = card.y() - st::introLookTitleSkip - font->height;
	if (top < st::introLookTitleSkip / 2) {
		// No room above the form on a small window.
		return;
	}
	p.setFont(font);
	p.setPen(TitleColor());
	const auto text = font->elided(title, card.width());
	p.drawText(
		QRect(card.x(), top, card.width(), font->height),
		Qt::AlignCenter,
		text);
}

// What the form looks like in the preview: a title, two fields and the
// button, drawn as plain shapes.
void PaintPreviewForm(QPainter &p, QRect card) {
	auto hq = PainterHighQualityEnabler(p);
	const auto left = card.x() + card.width() / 2 - 150;
	auto top = card.y() + st::introLookCardTop + 24;
	p.setPen(Qt::NoPen);

	p.setBrush(st::introTitleFg);
	p.drawRoundedRect(QRect(left + 50, top, 200, 18), 9, 9);
	top += 40;
	p.setBrush(st::introDescriptionFg);
	p.drawRoundedRect(QRect(left + 20, top, 260, 10), 5, 5);
	top += 20;
	p.drawRoundedRect(QRect(left + 60, top, 180, 10), 5, 5);
	top += 56;

	for (auto i = 0; i != 2; ++i) {
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(QRect(left, top, 300, 4), 2, 2);
		top += 56;
	}
	top += 24;
	p.setBrush(st::activeButtonBg);
	p.drawRoundedRect(QRect(left, top, 300, 42), 8, 8);
}

void PaintPreview(QPainter &p, QSize size, crl::time now) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = float64(st::introLookPreviewRadius);
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(QPointF(), QSizeF(size)), radius, radius);
	p.setClipPath(clip);

	// The screen of the virtual size is drawn centered and scaled to fill.
	const auto virtualSize = QSize(kPreviewWidth, kPreviewHeight);
	const auto scale = std::max(
		size.width() / float64(virtualSize.width()),
		size.height() / float64(virtualSize.height()));
	p.translate(
		(size.width() - virtualSize.width() * scale) / 2.,
		(size.height() - virtualSize.height() * scale) / 2.);
	p.scale(scale, scale);

	const auto contentTop = std::max(
		(virtualSize.height() - st::introHeight) / 2,
		st::introStepTopMin);
	PaintIntro(p, virtualSize, contentTop, now);
	PaintPreviewForm(p, IntroCardRect(virtualSize, contentTop));
}

[[nodiscard]] QString ColorText(const QString &hex) {
	return hex.isEmpty()
		? Tr("By theme", "По теме")
		: (u"#"_q + hex);
}

} // namespace

bool IntroDecorated() {
	const auto &settings = AyuSettings::getInstance();
	return (settings.introBackground() != Background::Plain)
		|| settings.introCard()
		|| !settings.introTitle().trimmed().isEmpty();
}

bool IntroAnimated() {
	return AyuSettings::getInstance().introBackground() == Background::Glow;
}

QRect IntroCardRect(QSize outer, int contentTop) {
	const auto width = std::min(st::introLookCardWidth, outer.width());
	return QRect(
		(outer.width() - width) / 2,
		contentTop - st::introLookCardTop,
		width,
		st::introLookCardTop + st::introLookCardBottom);
}

void PaintIntro(
		QPainter &p,
		QSize outer,
		int contentTop,
		crl::time now) {
	const auto &settings = AyuSettings::getInstance();
	const auto rect = QRect(QPoint(), outer);
	switch (settings.introBackground()) {
	case Background::Plain: p.fillRect(rect, st::windowBg); break;
	case Background::Gradient: PaintGradient(p, rect); break;
	case Background::Glow: PaintGlow(p, rect, now); break;
	}
	const auto card = IntroCardRect(outer, contentTop);
	if (settings.introCard()) {
		PaintCard(p, card);
	}
	PaintTitle(p, card, settings.introTitle().trimmed());
}

void IntroLookBox(not_null<Ui::GenericBox*> box) {
	auto &settings = AyuSettings::getInstance();
	const auto container = box->verticalLayout();

	box->setTitle(rpl::single(Tr("Login screen", "Экран входа")));
	box->setWidth(st::introLookBoxWidth);

	// The live preview.
	const auto preview = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::boxRowPadding + style::margins(0, st::boxLittleSkip, 0, 0));
	preview->resize(
		st::introLookBoxWidth - st::boxRowPadding.left() - st::boxRowPadding.right(),
		st::introLookPreviewHeight);
	preview->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(preview);
		PaintPreview(p, preview->size(), crl::now());
	}, preview->lifetime());
	rpl::merge(
		settings.introLookChanges(),
		style::PaletteChanged()
	) | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());
	const auto timer = preview->lifetime().make_state<base::Timer>([=] {
		if (IntroAnimated()) {
			preview->update();
		}
	});
	timer->callEach(33);

	// The background.
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("Background", "Фон")));
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		int(settings.introBackground()));
	const auto addRadio = [&](Background value, QString text) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				int(value),
				text,
				st::defaultBoxCheckbox),
			st::boxRowPadding + style::margins(0, 0, 0, st::boxLittleSkip));
	};
	addRadio(
		Background::Plain,
		Tr("Plain, like Telegram", "Обычный, как в Telegram"));
	addRadio(Background::Gradient, Tr("Gradient", "Градиент"));
	addRadio(
		Background::Glow,
		Tr("Glow that slowly moves", "Сияние, которое медленно движется"));
	group->setChangedCallback([](int value) {
		AyuSettings::getInstance().setIntroBackground(Background(value));
	});

	// The colors of the gradient and the glow.
	const auto addColor = [&](
			QString title,
			Fn<QString()> current,
			Fn<QColor()> fallback,
			Fn<void(QString)> save) {
		const auto button = container->add(object_ptr<Ui::SettingsButton>(
			container,
			rpl::single(title),
			st::settingsButtonNoIcon));
		const auto value = Ui::CreateChild<Ui::RpWidget>(button);
		value->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto swatch = st::introLookTitleSkip;
		const auto font = st::settingsButtonNoIcon.style.font;
		rpl::merge(
			button->sizeValue() | rpl::to_empty,
			settings.introLookChanges()
		) | rpl::on_next([=] {
			const auto size = button->size();
			const auto text = ColorText(current());
			const auto width = font->width(text) + swatch + swatch / 2;
			value->setGeometry(
				size.width() - st::settingsButtonNoIcon.padding.right() - width,
				0,
				width,
				size.height());
			value->update();
		}, value->lifetime());
		value->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(value);
			auto hq = PainterHighQualityEnabler(p);
			const auto top = (value->height() - swatch) / 2;
			p.setPen(QPen(st::windowBgOver->c, st::lineWidth));
			p.setBrush(Parse(current(), fallback()));
			p.drawEllipse(QRect(0, top, swatch, swatch));
			p.setFont(font);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				QRect(swatch + swatch / 2, 0, value->width(), value->height()),
				Qt::AlignLeft | Qt::AlignVCenter,
				ColorText(current()));
		}, value->lifetime());
		button->setClickedCallback([=] {
			const auto start = Parse(current(), fallback());
			box->uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> inner) {
				const auto editor = inner->addRow(object_ptr<ColorEditor>(
					inner,
					ColorEditor::Mode::HSL,
					start));
				const auto done = [=] {
					save(editor->color().name(QColor::HexRgb).mid(1).toUpper());
					inner->closeBox();
				};
				editor->submitRequests(
				) | rpl::on_next(done, editor->lifetime());
				inner->setFocusCallback([=] {
					editor->setInnerFocus();
				});
				inner->addButton(tr::lng_settings_save(), done);
				inner->addButton(tr::lng_cancel(), [=] {
					inner->closeBox();
				});
				inner->setWidth(editor->width());
			}));
		});
	};
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("Colors", "Цвета")));
	addColor(
		Tr("Main color", "Основной цвет"),
		[] { return AyuSettings::getInstance().introColor1(); },
		AutoFirstColor,
		[](QString color) {
			AyuSettings::getInstance().setIntroColor1(color);
		});
	addColor(
		Tr("Second color", "Второй цвет"),
		[] { return AyuSettings::getInstance().introColor2(); },
		AutoSecondColor,
		[](QString color) {
			AyuSettings::getInstance().setIntroColor2(color);
		});
	container->add(object_ptr<Ui::SettingsButton>(
		container,
		rpl::single(Tr("Colors by theme", "Цвета по теме")),
		st::settingsButtonNoIcon
	))->setClickedCallback([] {
		auto &settings = AyuSettings::getInstance();
		settings.setIntroColor1(QString());
		settings.setIntroColor2(QString());
	});

	// The card and the title.
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("Form", "Форма")));
	const auto card = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			Tr(
				"Put the form on a card",
				"Форма на карточке с тенью"),
			settings.introCard(),
			st::defaultBoxCheckbox),
		st::boxRowPadding + style::margins(0, 0, 0, st::boxLittleSkip));
	card->checkedChanges() | rpl::on_next([](bool checked) {
		AyuSettings::getInstance().setIntroCard(checked);
	}, card->lifetime());

	const auto title = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(Tr("Title above the form", "Надпись над формой")),
		settings.introTitle()));
	title->setMaxLength(48);
	title->changes() | rpl::on_next([=] {
		AyuSettings::getInstance().setIntroTitle(
			title->getLastText().trimmed());
	}, title->lifetime());
	Ui::AddSkip(container);

	box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
	});
	box->addLeftButton(
		rpl::single(Tr("Reset", "Сбросить")),
		[=] {
			auto &settings = AyuSettings::getInstance();
			settings.resetIntroLook();
			group->setValue(int(settings.introBackground()));
			card->setChecked(settings.introCard());
			title->setText(settings.introTitle());
		});
}

} // namespace HoursGram::Customization
