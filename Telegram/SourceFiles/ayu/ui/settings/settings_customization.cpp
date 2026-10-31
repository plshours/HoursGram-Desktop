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
#include "styles/style_hoursgram_customization.h"
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
#include "ui/widgets/buttons.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;
using namespace rpl::mappers;

namespace {

using HoursGram::Customization::ApplyCustomPalette;
using HoursGram::Customization::ColorToHex;
using HoursGram::Customization::CurrentKeyColors;
using HoursGram::Customization::HexToColor;
using HoursGram::Customization::IsCurrentThemeOverride;
using HoursGram::Customization::KeyOverrideId;
using HoursGram::Customization::PaletteGroups;
using HoursGram::Customization::PaletteKey;
using HoursGram::Customization::PaletteKeys;
using HoursGram::Customization::PaletteSlots;
using HoursGram::Customization::ResetToShippedPalette;

// A color picker for one value. `alpha` lets the color be see-through,
// which shadows, overlays and service messages need.
void ShowColorBox(
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		QColor current,
		bool alpha,
		Fn<void(QColor)> save,
		Fn<void()> reset) {
	controller->show(Box([=, title = std::move(title)](
			not_null<Ui::GenericBox*> box) mutable {
		box->setTitle(std::move(title));
		const auto editor = box->addRow(
			object_ptr<ColorEditor>(
				box,
				alpha ? ColorEditor::Mode::RGBA : ColorEditor::Mode::HSL,
				current));
		const auto done = [=] {
			save(editor->color());
			box->closeBox();
		};
		editor->submitRequests(
		) | rpl::on_next(done, editor->lifetime());
		box->setFocusCallback([=] {
			editor->setInnerFocus();
		});
		box->addButton(tr::lng_settings_save(), done);
		if (reset) {
			box->addLeftButton(tr::hoursgram_PaletteResetOne(), [=] {
				reset();
				box->closeBox();
			});
		}
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
		box->setWidth(editor->width());
	}));
}

// The colors of single palette keys. With `search` the list gets a field
// that filters it by the key name, which the full list of keys needs.
void ShowPaletteKeysBox(
		not_null<Window::SessionController*> controller,
		Fn<rpl::producer<QString>()> title,
		std::vector<PaletteKey> keys,
		bool search) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		auto *settings = &AyuSettings::getInstance();
		box->setTitle(title());
		box->setWidth(st::boxWideWidth);
		box->setMaxHeight(st::boxWideWidth * 2);
		box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

		// Worked out once for the whole list, and again after each pick.
		const auto colors = box->lifetime().make_state<
			rpl::variable<base::flat_map<QString, QString>>
		>(CurrentKeyColors());
		settings->paletteOverridesChanges(
		) | rpl::on_next([=] {
			*colors = CurrentKeyColors();
		}, box->lifetime());

		const auto query = box->lifetime().make_state<
			rpl::variable<QString>
		>();
		if (search) {
			const auto field = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				tr::hoursgram_PaletteSearch()));
			field->changes() | rpl::on_next([=] {
				*query = field->getLastText().trimmed();
			}, field->lifetime());
			box->setFocusCallback([=] {
				field->setFocusFast();
			});
		}

		const auto content = box->verticalLayout();
		for (const auto &entry : keys) {
			const auto key = entry.key;
			const auto keyTitle = entry.title;
			const auto id = KeyOverrideId(key);
			const auto wrap = content->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					content,
					object_ptr<Ui::VerticalLayout>(content)));
			const auto button = AddButtonWithLabel(
				wrap->entity(),
				keyTitle(),
				colors->value(
				) | rpl::map([=](const base::flat_map<QString, QString> &map) {
					const auto i = map.find(key);
					if (i == map.end()) {
						return QString();
					}
					// A star marks the colors that were picked by hand.
					const auto mark = settings->paletteOverride(id).isEmpty()
						? QString()
						: u"* "_q;
					return mark + u"#"_q + i->second;
				}),
				st::hoursgramPaletteButton);
			button->setClickedCallback([=] {
				const auto &map = colors->current();
				const auto i = map.find(key);
				const auto current = (i != map.end())
					? HexToColor(i->second)
					: st::windowBg->c;
				ShowColorBox(
					controller,
					keyTitle(),
					current,
					true,
					[=](QColor color) {
						settings->setPaletteOverride(id, ColorToHex(color));
						ApplyCustomPalette(true);
					},
					[=] {
						settings->setPaletteOverride(id, QString());
						ApplyCustomPalette(true);
					});
			});
			if (search) {
				query->value(
				) | rpl::on_next([=](const QString &text) {
					wrap->toggle(
						text.isEmpty()
							|| key.contains(text, Qt::CaseInsensitive),
						anim::type::instant);
				}, wrap->lifetime());
			}
		}
	}));
}

// The five colors the whole interface is built from come first; the
// groups after them reach every single color of the palette, so that the
// bubbles, their tails, the input bar and the chat list can be set apart.
void BuildPalette(SectionBuilder &builder) {
	const auto controller = builder.controller();
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle(tr::hoursgram_CustomizationPalette());

	for (const auto &slot : PaletteSlots()) {
		const auto id = slot.id;
		const auto base = slot.base;
		const auto title = slot.title;
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
				ShowColorBox(
					controller,
					title(),
					QColor(u"#"_q + (picked.isEmpty() ? base : picked)),
					false,
					[=](QColor color) {
						settings->setPaletteOverride(
							id,
							color.name(QColor::HexRgb).mid(1).toUpper());
						ApplyCustomPalette(true);
					},
					[=] {
						settings->setPaletteOverride(id, QString());
						ApplyCustomPalette(true);
					});
			},
		});
	}
	builder.addDividerText(tr::hoursgram_CustomizationPaletteAbout());

	builder.addSkip();
	builder.addSubsectionTitle(tr::hoursgram_PaletteDetailed());
	for (const auto &group : PaletteGroups()) {
		const auto title = group.title;
		const auto keys = group.keys;
		builder.addButton({
			.id = u"hoursgram/palette/"_q + group.id,
			.title = group.title(),
			.st = &st::settingsButtonNoIcon,
			.onClick = [=] {
				ShowPaletteKeysBox(controller, title, keys, false);
			},
		});
	}
	builder.addButton({
		.id = u"hoursgram/palette/all"_q,
		.title = tr::hoursgram_PaletteGroupAll(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			auto keys = std::vector<PaletteKey>();
			for (const auto &key : PaletteKeys()) {
				keys.push_back({ key, [=] { return rpl::single(key); } });
			}
			ShowPaletteKeysBox(
				controller,
				[] { return tr::hoursgram_PaletteGroupAll(); },
				std::move(keys),
				true);
		},
	});

	builder.addButton({
		.id = u"hoursgram/colorReset"_q,
		.title = tr::hoursgram_ColorReset(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			// Dropping the picks is not enough on its own: the running
			// palette still holds them, so the shipped one is applied
			// again. Only the palette is swapped - going through the
			// theme would also throw away the chat background. The picks
			// made for the other themes are kept.
			settings->resetPaletteOverrides(IsCurrentThemeOverride);
			ResetToShippedPalette();
			controller->showToast(tr::hoursgram_ColorResetDone(tr::now));
		},
	});
	builder.addDividerText(tr::hoursgram_PaletteDetailedAbout());
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
	// The sides are only worth asking about while tails are drawn at all.
	ayu.addSettingToggle({
		.id = u"hoursgram/incomingMessageTail"_q,
		.title = tr::hoursgram_TailIncoming(),
		.getter = &AyuSettings::incomingMessageTail,
		.setter = &AyuSettings::setIncomingMessageTail,
		.shown = settings->removeMessageTailValue() | rpl::map(!_1),
	});
	ayu.addSettingToggle({
		.id = u"hoursgram/outgoingMessageTail"_q,
		.title = tr::hoursgram_TailOutgoing(),
		.getter = &AyuSettings::outgoingMessageTail,
		.setter = &AyuSettings::setOutgoingMessageTail,
		.shown = settings->removeMessageTailValue() | rpl::map(!_1),
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
