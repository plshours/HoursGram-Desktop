// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_main.h"

#include "settings/sections/settings_main.h"
#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/ayu_logo.h"
#include "ayu/ui/settings/settings_appearance.h"
#include "ayu/ui/settings/settings_ayu.h"
#include "ayu/ui/settings/settings_chats.h"
#include "ayu/ui/settings/settings_customization.h"
#include "ayu/ui/settings/settings_local_nft.h"
#include "ayu/ui/settings/settings_filters.h"
#include "ayu/ui/settings/settings_general.h"
#include "ayu/ui/settings/settings_other.h"
#include "core/version.h"
#include "lang/lang_instance.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QDesktopServices>

namespace Settings {

using namespace Builder;

namespace {

// The translations of the keys of AyuGram come from its own servers, and the
// keys of this client have none, so the page names its sections itself.
[[nodiscard]] bool UsesRussian() {
	const auto &lang = Lang::GetInstance();
	return lang.id().startsWith(u"ru"_q) || lang.baseId().startsWith(u"ru"_q);
}

[[nodiscard]] rpl::producer<QString> Label(
		const QString &english,
		const QString &russian) {
	return rpl::single(UsesRussian() ? russian : english);
}

void BuildLogo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto logo = object_ptr<Ui::RpWidget>(ctx.container);
		const auto logoRaw = logo.data();
		logoRaw->resize(
			QSize(st::hoursgramSettingsLogoSize,
				st::hoursgramSettingsLogoSize));
		logoRaw->setNaturalWidth(st::hoursgramSettingsLogoSize);
		logoRaw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(logoRaw);
			const auto image = AyuAssets::currentAppLogo();
			if (!image.isNull()) {
				const auto size = st::hoursgramSettingsLogoSize;
				const auto scaled = image.scaled(
					size * style::DevicePixelRatio(),
					size * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
				p.drawImage(QRect(0, 0, size, size), scaled);
			}
		}, logoRaw->lifetime());
		return { .widget = std::move(logo), .align = style::al_top };
	});
}

void BuildVersionInfo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(
					QString("HorusGram Desktop v")
					+ QString::fromLatin1(AppVersionStr)),
				st::boxTitle),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				Label(
					u"An unofficial client based on Telegram Desktop and AyuGram."_q,
					u"Неофициальный клиент на основе Telegram Desktop и AyuGram."_q),
				st::centeredBoxLabel),
			.margin = QMargins(
				st::boxRowPadding.left(),
				0,
				st::boxRowPadding.right(),
				0),
			.align = style::al_top,
		};
	});
}

void BuildCategories(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(Label(u"Sections"_q, u"Разделы"_q));

	// One icon for each section, none of them twice.
	builder.addSectionButton({
		.title = Label(u"Ghost mode and privacy"_q, u"Режим призрака и приватность"_q),
		.targetSection = AyuGhost::Id(),
		.icon = { &st::menuIconStealth },
	});
	builder.addSectionButton({
		.title = Label(u"Message filters"_q, u"Фильтры сообщений"_q),
		.targetSection = AyuFilters::Id(),
		.icon = { &st::menuIconTagFilter },
	});
	builder.addSectionButton({
		.title = Label(u"General"_q, u"Основные настройки"_q),
		.targetSection = AyuGeneral::Id(),
		.icon = { &st::menuIconSettings },
	});
	builder.addSectionButton({
		.title = Label(u"Appearance"_q, u"Внешний вид"_q),
		.targetSection = AyuAppearance::Id(),
		.icon = { &st::menuIconPalette },
	});
	builder.addSectionButton({
		.title = Label(u"Fonts and scale"_q, u"Шрифты и масштаб"_q),
		.targetSection = AyuCustomization::Id(),
		.icon = { &st::menuIconFont },
	});
	builder.addSectionButton({
		.title = Label(u"Local gifts"_q, u"Локальные подарки"_q),
		.targetSection = AyuLocalNft::Id(),
		.icon = { &st::menuIconGiftPremium },
	});
	builder.addSectionButton({
		.title = Label(u"Chats and messages"_q, u"Чаты и сообщения"_q),
		.targetSection = AyuChats::Id(),
		.icon = { &st::menuIconChatBubble },
	});
	builder.addSectionButton({
		.title = Label(u"More"_q, u"Прочее"_q),
		.targetSection = AyuOther::Id(),
		.icon = { &st::menuIconFave },
	});
}

void BuildLinks(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(Label(u"Links"_q, u"Ссылки"_q));

	builder.addButton({
		.id = u"hoursgram/source"_q,
		.title = Label(u"Source code on GitHub"_q, u"Исходный код на GitHub"_q),
		.icon = { &st::menuIconIpAddress },
		.label = rpl::single(QString("HorusGram Desktop")),
		.onClick = [=] {
			QDesktopServices::openUrl(QString(
				"https://github.com/plshours/HoursGram-Desktop"));
		},
	});

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuMain::Id(),
	.parentId = MainId(),
	.title = &tr::ayu_AyuPreferences,
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildLogo(builder);
	builder.addSkip();
	BuildVersionInfo(builder);
	BuildCategories(builder);
	BuildLinks(builder);
});

} // namespace

rpl::producer<QString> AyuMain::title() {
	return rpl::single(QString(""));
}

AyuMain::AyuMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuMainId() {
	return AyuMain::Id();
}

} // namespace Settings
