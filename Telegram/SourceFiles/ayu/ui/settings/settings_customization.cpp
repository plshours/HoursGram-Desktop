// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_customization.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/boxes/font_selector.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "boxes/background_box.h"
#include "boxes/background_preview_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_wall_paper.h"
#include "hoursgram/customization/customization_palette.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "storage/localstorage.h"
#include "ui/boxes/choose_font_box.h"
#include "ui/chat/attach/attach_extensions.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/color_editor.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

using HoursGram::Customization::ApplyCustomPalette;
using HoursGram::Customization::PaletteSlots;
using HoursGram::Customization::ResetToShippedPalette;

// Only the colors the palette is actually built from are editable, so a
// pick stays consistent everywhere that color is used.
void BuildPalette(SectionBuilder &builder) {
	const auto controller = builder.controller();
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle(tr::hoursgram_CustomizationPalette());

	for (const auto &slot : PaletteSlots()) {
		const auto id = slot.id;
		const auto base = slot.base;
		builder.addButton({
			.id = u"hoursgram/color/"_q + id,
			.title = slot.title(),
			.st = &st::settingsButtonNoIcon,
			.label = rpl::single(
				rpl::empty
			) | rpl::then(
				settings->paletteOverridesChanges()
			) | rpl::map([=](auto&&) {
				const auto picked = settings->paletteOverride(id);
				return u"#"_q + (picked.isEmpty() ? base : picked);
			}),
			.onClick = [=] {
				const auto picked = settings->paletteOverride(id);
				const auto current = QColor(
					u"#"_q + (picked.isEmpty() ? base : picked));
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					const auto editor = box->addRow(
						object_ptr<ColorEditor>(
							box,
							ColorEditor::Mode::HSL,
							current));
					const auto save = [=] {
						const auto color = editor->color();
						settings->setPaletteOverride(
							id,
							color.name(QColor::HexRgb).mid(1).toUpper());
						ApplyCustomPalette(true);
						box->closeBox();
					};
					editor->submitRequests(
					) | rpl::on_next(save, editor->lifetime());
					box->setFocusCallback([=] {
						editor->setInnerFocus();
					});
					box->addButton(tr::lng_settings_save(), save);
					box->addButton(tr::lng_cancel(), [=] {
						box->closeBox();
					});
					box->setWidth(editor->width());
				}));
			},
		});
	}

	builder.addButton({
		.id = u"hoursgram/colorReset"_q,
		.title = tr::hoursgram_ColorReset(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			// Dropping the picks is not enough on its own: the running
			// palette still holds them, so the shipped one is applied
			// again. Only the palette is swapped - going through the
			// theme would also throw away the chat background.
			settings->resetPaletteOverrides();
			ResetToShippedPalette();
			controller->showToast(tr::hoursgram_ColorResetDone(tr::now));
		},
	});
	builder.addDividerText(tr::hoursgram_CustomizationPaletteAbout());
}

void BuildBackground(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto parent = static_cast<QWidget*>(builder.container());

	builder.addSkip();
	builder.addSubsectionTitle(tr::hoursgram_CustomizationBackground());

	builder.addButton({
		.id = u"hoursgram/bgFromFile"_q,
		.title = tr::hoursgram_BackgroundFromFile(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			auto filters = QStringList(
				u"Images (*"_q
				+ Ui::ImageExtensions().join(u" *"_q)
				+ u")"_q);
			filters.push_back(FileDialog::AllFilesFilter());
			const auto chosen = crl::guard(controller, [=](
					const FileDialog::OpenResult &result) {
				auto image = Images::Read({
					.path = result.paths.isEmpty()
						? QString()
						: result.paths.front(),
					.content = result.remoteContent,
					.forceOpaque = true,
				}).image;
				if (image.isNull()
					|| image.width() <= 0
					|| image.height() <= 0) {
					return;
				}
				auto local = Data::CustomWallPaper();
				local.setLocalImageAsThumbnail(
					std::make_shared<Image>(std::move(image)));
				controller->show(
					Box<BackgroundPreviewBox>(controller, local));
			});
			FileDialog::GetOpenPath(
				parent,
				tr::lng_choose_image(tr::now),
				filters.join(u";;"_q),
				chosen);
		},
	});
	builder.addButton({
		.id = u"hoursgram/bgFromGallery"_q,
		.title = tr::hoursgram_BackgroundFromGallery(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->show(Box<BackgroundBox>(controller));
		},
	});
	builder.addButton({
		.id = u"hoursgram/bgReset"_q,
		.title = tr::hoursgram_BackgroundReset(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			Window::Theme::Background()->set(Data::DefaultWallPaper());
		},
	});
}

void BuildMessages(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSkip();
	builder.addSubsectionTitle(tr::hoursgram_CustomizationMessages());

	ayu.addSettingToggle({
		.id = u"hoursgram/removeMessageTail"_q,
		.title = tr::ayu_RemoveMessageTail(),
		.getter = &AyuSettings::removeMessageTail,
		.setter = &AyuSettings::setRemoveMessageTail,
	});

	const auto controller = builder.controller();
	ayu.addSlider({
		.id = u"hoursgram/bubbleRadius"_q,
		.title = tr::hoursgram_BubbleRadius(),
		.steps = 17,
		.current = settings->messageBubbleRadius(),
		.indexToValue = [](int index) { return index; },
		.onFinalChanged = [=](int index) {
			settings->setMessageBubbleRadius(index);
			Ui::SetAppliedBubbleRadius(index);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [](int index) { return QString::number(index); },
	});
}

// Everything that decides how text looks, in one place: the family the
// whole interface is drawn with, the one code blocks use, and how large
// the result ends up on screen.
void BuildFonts(SectionBuilder &builder) {
	const auto controller = builder.controller();
	auto *settings = &AyuSettings::getInstance();

	builder.addSkip();
	builder.addSubsectionTitle(tr::hoursgram_CustomizationFonts());

	builder.add([](const WidgetContext &ctx) {
		const auto container = ctx.container;
		const auto controller = ctx.controller;
		const auto core = &Core::App().settings();
		const auto family = container->lifetime().make_state<
			rpl::variable<QString>
		>(core->customFontFamily());
		auto label = family->value() | rpl::map([](QString family) {
			return family.isEmpty()
				? tr::lng_font_default(tr::now)
				: (family == style::SystemFontTag())
				? tr::lng_font_system(tr::now)
				: family;
		});
		auto wrap = object_ptr<Ui::VerticalLayout>(container);
		const auto button = AddButtonWithLabel(
			wrap.data(),
			tr::hoursgram_FontInterface(),
			std::move(label),
			st::settingsButtonNoIcon);
		button->setClickedCallback([=] {
			// The font is picked up once, while the styles are built at
			// startup, so there is no way to show it without starting over.
			const auto save = [=](QString chosen) {
				*family = chosen;
				Core::App().settings().setCustomFontFamily(chosen);
				Local::writeSettings();
				Core::Restart();
			};
			const auto theme = std::shared_ptr<Ui::ChatTheme>(
				Window::Theme::DefaultChatThemeOn(button->lifetime()));
			const auto generateBg = [=] {
				const auto size = st::boxWidth;
				const auto ratio = style::DevicePixelRatio();
				auto result = QImage(
					QSize(size, size) * ratio,
					QImage::Format_ARGB32_Premultiplied);
				auto p = QPainter(&result);
				Window::SectionWidget::PaintBackground(
					p,
					theme.get(),
					QSize(size, size * 3),
					QRect(0, 0, size, size));
				p.end();
				return result;
			};
			controller->show(
				Box(Ui::ChooseFontBox, generateBg, family->current(), save));
		});
		return SectionBuilder::WidgetToAdd{ .widget = std::move(wrap) };
	}, [] {
		return SearchEntry{
			.id = u"hoursgram/fontInterface"_q,
			.title = tr::hoursgram_FontInterface(tr::now),
			.keywords = { u"font"_q, u"family"_q, u"text"_q },
		};
	});

	builder.addButton({
		.id = u"hoursgram/fontMono"_q,
		.title = tr::ayu_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->monoFontValue() | rpl::map([](QString font) {
			return font.isEmpty() ? tr::ayu_FontDefault(tr::now) : font;
		}),
		.onClick = [=] {
			AyuUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					AyuSettings::getInstance().setMonoFont(font);
				});
		},
	});
	builder.addDividerText(tr::hoursgram_CustomizationFontsAbout());

	if (HasInterfaceScale()) {
		builder.addSkip();
		builder.add([](const WidgetContext &ctx) {
			const auto window = &ctx.controller->window();
			auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
			SetupInterfaceScale(window, wrap.data(), false);
			return SectionBuilder::WidgetToAdd{ .widget = std::move(wrap) };
		}, [] {
			return SearchEntry{
				.id = u"hoursgram/scale"_q,
				.title = tr::lng_settings_default_scale(tr::now),
				.keywords = { u"zoom"_q, u"size"_q, u"interface"_q },
			};
		});
		builder.addSkip();
	}
}

const auto kMeta = BuildHelper({
	.id = AyuCustomization::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryCustomization,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildPalette(builder);
	BuildFonts(builder);
	BuildBackground(builder);
	BuildMessages(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuCustomization::title() {
	return tr::ayu_CategoryCustomization();
}

AyuCustomization::AyuCustomization(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuCustomization::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuCustomizationId() {
	return AyuCustomization::Id();
}

} // namespace Settings
