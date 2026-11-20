/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/customization_palette.h"

#include "ayu/ayu_settings.h"
#include "data/data_wall_paper.h"
#include "hoursgram/music/music_common.h"
#include "lang_auto.h"
#include "window/themes/window_theme.h"

#include "base/flat_map.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>

#include <cmath>
#include <cstdlib>

namespace HoursGram::Customization {
namespace {

// The palette that is shipped for the theme that is on: HorusGram, or the
// dark or the light look of Telegram for macOS.
[[nodiscard]] QString PalettePath() {
	switch (AyuSettings::getInstance().clientTheme()) {
	case 2: return u":/gui/hoursgram-macos-dark.tdesktop-palette"_q;
	case 3: return u":/gui/hoursgram-macos.tdesktop-palette"_q;
	case 4: return u":/gui/hoursgram-graphite.tdesktop-palette"_q;
	case AyuSettings::kWindowTheme:
		return u":/gui/hoursgram-window.tdesktop-palette"_q;
	}
	return u":/gui/hoursgram.tdesktop-palette"_q;
}

[[nodiscard]] QString Normalized(QString color) {
	const auto trimmed = color.trimmed();
	static const auto check = QRegularExpression(
		u"^#?[0-9A-Fa-f]{6}$"_q);
	if (!check.match(trimmed).hasMatch()) {
		return QString();
	}
	return trimmed.startsWith(u'#')
		? trimmed.mid(1).toUpper()
		: trimmed.toUpper();
}

[[nodiscard]] Fn<rpl::producer<QString>()> Label(
		const char *english,
		const char *russian) {
	return [=] {
		return rpl::single(HoursGram::Music::Tr(english, russian));
	};
}

// HorusGram WINDOW is the usual Telegram client, so it is recolored part by
// part: each slot sets the palette colors of one part and the shades that
// go with them. The base colors are those of the shipped palette, built by
// tools/window_palette.py: black panels, white for what is active.
[[nodiscard]] const std::vector<PaletteSlot> &WindowSlots() {
	static const auto result = std::vector<PaletteSlot>{
		// The window. Black like the frontend it follows; each part below
		// can take a color of its own.
		{
			.id = u"window/windowBg"_q,
			.base = u"000000"_q,
			.title = Label("Window background", "Фон окна"),
			.keys = {
				{ u"windowBg"_q },
				{ u"dialogsBg"_q },
				{ u"mainMenuBg"_q },
				{ u"topBarBg"_q },
				{ u"titleBg"_q },
				{ u"titleBgActive"_q },
				{ u"sideBarBg"_q },
				{ u"windowShadowFgFallback"_q },
				{ u"windowBgOver"_q, 7 },
				{ u"windowBgRipple"_q, 10 },
			},
			.group = Label("Window", "Окно"),
		},
		{
			.id = u"window/text"_q,
			.base = u"FFFFFF"_q,
			.title = Label("Text", "Текст"),
			.keys = {
				{ u"windowFg"_q },
				{ u"windowFgOver"_q },
				{ u"dialogsNameFg"_q },
				{ u"boxTitleFg"_q },
				{ u"introTitleFg"_q },
			},
		},
		{
			.id = u"window/boldText"_q,
			.base = u"FFFFFF"_q,
			.title = Label(
				"Menu and settings items",
				"Пункты меню и настроек"),
			.keys = {
				{ u"windowBoldFg"_q },
				{ u"windowBoldFgOver"_q },
			},
		},
		{
			.id = u"window/subText"_q,
			.base = u"8C8C8C"_q,
			.title = Label("Secondary text", "Второстепенный текст"),
			.keys = {
				{ u"windowSubTextFg"_q },
				{ u"windowSubTextFgOver"_q, 8 },
				{ u"dialogsTextFg"_q, 8 },
				{ u"introDescriptionFg"_q, 8 },
			},
		},
		{
			.id = u"window/accent"_q,
			.base = u"FFFFFF"_q,
			.title = Label(
				"Accent: switches, links, the NEW badge",
				"Акцент: переключатели, ссылки, «НОВОЕ»"),
			.keys = {
				{ u"windowBgActive"_q },
				{ u"activeLineFg"_q, 8 },
				{ u"windowActiveTextFg"_q, 25 },
				{ u"emojiIconFgActive"_q, 15 },
				{ u"sideBarIconFgActive"_q, 15 },
				{ u"sideBarTextFgActive"_q, 15 },
				{ u"sideBarBadgeBg"_q, 15 },
			},
		},
		{
			.id = u"window/hover"_q,
			.base = u"111111"_q,
			.title = Label("Hover", "Наведение"),
			.keys = {
				{ u"windowBgOver"_q },
				{ u"windowBgRipple"_q, 4 },
				{ u"lightButtonBgOver"_q },
				{ u"lightButtonBgRipple"_q, 4 },
			},
		},
		{
			.id = u"window/title"_q,
			.base = u"000000"_q,
			.title = Label("Window title", "Заголовок окна"),
			.keys = {
				{ u"titleBgActive"_q },
				{ u"titleBg"_q, -6 },
			},
		},

		// Buttons.
		{
			.id = u"window/button"_q,
			.base = u"FFFFFF"_q,
			.title = Label("Buttons", "Кнопки"),
			.keys = {
				{ u"activeButtonBg"_q },
				{ u"activeButtonBgOver"_q, 6 },
				{ u"activeButtonBgRipple"_q, 12 },
			},
			.group = Label("Buttons", "Кнопки"),
		},
		{
			.id = u"window/buttonText"_q,
			.base = u"000000"_q,
			.title = Label("Text on buttons", "Текст на кнопках"),
			.keys = {
				{ u"activeButtonFg"_q },
				{ u"activeButtonFgOver"_q },
			},
		},
		{
			.id = u"window/lightButton"_q,
			.base = u"FFFFFF"_q,
			.title = Label(
				"Text buttons in boxes",
				"Текстовые кнопки в окнах"),
			.keys = {
				{ u"lightButtonFg"_q },
			},
		},
		{
			.id = u"window/attention"_q,
			.base = u"EF4444"_q,
			.title = Label("Dangerous actions", "Опасные действия"),
			.keys = {
				{ u"attentionButtonFg"_q },
				{ u"attentionButtonFgOver"_q, 8 },
			},
		},

		// The input field.
		{
			.id = u"window/compose"_q,
			.base = u"141414"_q,
			.title = Label("Input field background", "Фон поля ввода"),
			.keys = {
				{ u"historyComposeAreaBg"_q },
				{ u"historyComposeButtonBg"_q },
				{ u"historyComposeButtonBgOver"_q, 5 },
				{ u"historyComposeButtonBgRipple"_q, 9 },
			},
			.group = Label("Input field", "Поле ввода"),
		},
		{
			.id = u"window/composeText"_q,
			.base = u"FFFFFF"_q,
			.title = Label("Message text", "Текст сообщения"),
			.keys = {
				{ u"historyComposeAreaFg"_q },
			},
		},
		{
			.id = u"window/placeholder"_q,
			.base = u"737373"_q,
			.title = Label("Hint in the field", "Подсказка в поле"),
			.keys = {
				{ u"placeholderFg"_q },
				{ u"placeholderFgActive"_q, -20 },
			},
		},
		{
			.id = u"window/composeIcons"_q,
			.base = u"8C8C8C"_q,
			.title = Label(
				"Icons: emoji, attach",
				"Иконки: эмодзи, скрепка"),
			.keys = {
				{ u"historyComposeIconFg"_q },
				{ u"emojiIconFg"_q },
				{ u"historyComposeIconFgOver"_q, 50 },
			},
		},
		{
			.id = u"window/send"_q,
			.base = u"FFFFFF"_q,
			.title = Label("Send button", "Кнопка «Отправить»"),
			.keys = {
				{ u"historySendIconFg"_q },
				{ u"historySendIconFgOver"_q, 10 },
			},
		},

		// The side menu: profile, groups, contacts, calls, settings.
		{
			.id = u"window/menuBg"_q,
			.base = u"000000"_q,
			.title = Label("Menu background", "Фон меню"),
			.keys = {
				{ u"mainMenuBg"_q },
			},
			.group = Label("Side menu and accounts", "Боковое меню и аккаунты"),
		},
		{
			.id = u"window/menuIcons"_q,
			.base = u"8C8C8C"_q,
			.title = Label(
				"Menu and settings icons",
				"Иконки меню и настроек"),
			.keys = {
				{ u"menuIconFg"_q },
				{ u"menuIconFgOver"_q, 50 },
			},
		},
		{
			.id = u"window/accounts"_q,
			.base = u"FFFFFF"_q,
			.title = Label(
				"Accounts and menu switches",
				"Аккаунты и переключатели меню"),
			.keys = {
				{ u"mainMenuCoverBg"_q },
				{ u"mainMenuCloudBg"_q, -20 },
			},
		},
		{
			.id = u"window/popupMenu"_q,
			.base = u"0F0F0F"_q,
			.title = Label(
				"Context menus and boxes",
				"Контекстные меню и окна"),
			.keys = {
				{ u"menuBg"_q },
				{ u"menuBgRipple"_q, 9 },
				{ u"boxBg"_q },
				{ u"boxSearchBg"_q },
			},
		},

		// Chats.
		{
			.id = u"window/chats"_q,
			.base = u"000000"_q,
			.title = Label("Chat list", "Список чатов"),
			.keys = {
				{ u"dialogsBg"_q },
				{ u"dialogsBgOver"_q, 5 },
				{ u"dialogsRippleBg"_q, 9 },
			},
			.group = Label("Chats", "Чаты"),
		},
		{
			.id = u"window/chatActive"_q,
			.base = u"1A1A1A"_q,
			.title = Label("Selected chat", "Выбранный чат"),
			.keys = {
				{ u"dialogsBgActive"_q },
				{ u"dialogsRippleBgActive"_q, 6 },
			},
		},
		{
			.id = u"window/unread"_q,
			.base = u"FFFFFF"_q,
			.title = Label("Unread counters", "Счётчики непрочитанных"),
			.keys = {
				{ u"dialogsUnreadBg"_q },
				{ u"dialogsUnreadBgOver"_q },
			},
		},
		{
			.id = u"window/folders"_q,
			.base = u"000000"_q,
			.title = Label("Folders bar", "Панель папок"),
			.keys = {
				{ u"sideBarBg"_q },
				{ u"sideBarBgRipple"_q, 6 },
				{ u"sideBarBgActive"_q, 10 },
			},
		},

		// Messages.
		{
			.id = u"window/msgIn"_q,
			.base = u"141414"_q,
			.title = Label("Incoming messages", "Входящие сообщения"),
			.keys = {
				{ u"msgInBg"_q },
			},
			.group = Label("Messages", "Сообщения"),
		},
		{
			.id = u"window/msgOut"_q,
			.base = u"262626"_q,
			.title = Label("Outgoing messages", "Исходящие сообщения"),
			.keys = {
				{ u"msgOutBg"_q },
			},
		},

		// Profile and settings.
		{
			.id = u"window/dividers"_q,
			.base = u"0A0A0A"_q,
			.title = Label(
				"Gaps between groups",
				"Промежутки между группами"),
			.keys = {
				{ u"boxDividerBg"_q },
			},
			.group = Label("Profile and settings", "Профиль и настройки"),
		},
		{
			.id = u"window/verified"_q,
			.base = u"FFFFFF"_q,
			.title = Label("Verified badge", "Галочка верификации"),
			.keys = {
				{ u"profileVerifiedCheckBg"_q },
			},
		},
	};
	return result;
}

} // namespace

const std::vector<PaletteSlot> &PaletteSlots() {
	// Every theme has slots of its own, with ids of their own, so a color
	// picked for one is never laid over another.
	static const auto macDark = std::vector<PaletteSlot>{
		{ u"macDark/windowBg"_q, u"18222D"_q, [] {
			return tr::hoursgram_ColorWindowBg();
		} },
		{ u"macDark/surface"_q, u"1E2C3B"_q, [] {
			return tr::hoursgram_ColorModalBg();
		} },
		{ u"macDark/accent"_q, u"2EA6FF"_q, [] {
			return tr::hoursgram_ColorAccent();
		} },
		{ u"macDark/accentLight"_q, u"62BCF9"_q, [] {
			return tr::hoursgram_ColorAccentLight();
		} },
		{ u"macDark/subtitle"_q, u"B1C3D5"_q, [] {
			return tr::hoursgram_ColorSubtitle();
		} },
	};
	static const auto macLight = std::vector<PaletteSlot>{
		{ u"macLight/surface"_q, u"F4F4F4"_q, [] {
			return tr::hoursgram_ColorModalBg();
		} },
		{ u"macLight/accent"_q, u"2481CC"_q, [] {
			return tr::hoursgram_ColorAccent();
		} },
		{ u"macLight/accentLight"_q, u"4BA3E2"_q, [] {
			return tr::hoursgram_ColorAccentLight();
		} },
		{ u"macLight/subtitle"_q, u"999999"_q, [] {
			return tr::hoursgram_ColorSubtitle();
		} },
	};
	static const auto graphite = std::vector<PaletteSlot>{
		{ u"graphite/windowBg"_q, u"181818"_q, [] {
			return tr::hoursgram_ColorWindowBg();
		} },
		{ u"graphite/modalBg"_q, u"121212"_q, [] {
			return tr::hoursgram_ColorModalBg();
		} },
		{ u"graphite/accent"_q, u"5B8CF5"_q, [] {
			return tr::hoursgram_ColorAccent();
		} },
		{ u"graphite/accentLight"_q, u"8EB2FA"_q, [] {
			return tr::hoursgram_ColorAccentLight();
		} },
		{ u"graphite/subtitle"_q, u"8E8E8E"_q, [] {
			return tr::hoursgram_ColorSubtitle();
		} },
	};
	const auto theme = AyuSettings::getInstance().clientTheme();
	if (theme == AyuSettings::kWindowTheme) {
		return WindowSlots();
	} else if (theme == 4) {
		return graphite;
	} else if (theme == 2) {
		return macDark;
	} else if (theme == 3) {
		return macLight;
	}
	static const auto result = std::vector<PaletteSlot>{
		{ u"windowBg"_q, u"343641"_q, [] {
			return tr::hoursgram_ColorWindowBg();
		} },
		{ u"modalBg"_q, u"282A34"_q, [] {
			return tr::hoursgram_ColorModalBg();
		} },
		{ u"accent"_q, u"8993D8"_q, [] {
			return tr::hoursgram_ColorAccent();
		} },
		{ u"accentLight"_q, u"AAB2ED"_q, [] {
			return tr::hoursgram_ColorAccentLight();
		} },
		{ u"subtitle"_q, u"B1B2B7"_q, [] {
			return tr::hoursgram_ColorSubtitle();
		} },
	};
	return result;
}

namespace {

// A hover or a pressed shade of `color`: `shift` percent of the way to
// white for a dark color and to black for a light one, so a white button
// gets a visible hover too. A negative shift always goes to black.
[[nodiscard]] QColor Shade(QColor color, int shift) {
	if (!shift) {
		return color;
	}
	const auto light = (color.lightness() > 160);
	const auto target = (shift > 0 && !light)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);
	const auto ratio = std::abs(shift) / 100.;
	const auto mix = [&](int a, int b) {
		return int(std::round(a + (b - a) * ratio));
	};
	return QColor(
		mix(color.red(), target.red()),
		mix(color.green(), target.green()),
		mix(color.blue(), target.blue()));
}

// Sets the named colors of `keys` to the given ones. A color that had an
// alpha keeps it, everything else on the line (the comment) stays too.
[[nodiscard]] QString WithKeys(
		const QString &text,
		const base::flat_map<QString, QString> &keys) {
	if (keys.empty()) {
		return text;
	}
	static const auto line = QRegularExpression(
		u"^(\\s*)([A-Za-z0-9_]+)(\\s*:\\s*)([^;\\n]*);"_q,
		QRegularExpression::MultilineOption);
	auto result = QString();
	result.reserve(text.size());
	auto from = qsizetype(0);
	auto i = line.globalMatch(text);
	while (i.hasNext()) {
		const auto match = i.next();
		const auto found = keys.find(match.captured(2));
		if (found == keys.end()) {
			continue;
		}
		const auto value = match.captured(4).trimmed();
		const auto alpha = (value.startsWith(u'#') && value.size() == 9)
			? value.mid(7)
			: QString();
		result += text.mid(from, match.capturedStart(4) - from);
		result += u"#"_q + found->second + alpha;
		from = match.capturedEnd(4);
	}
	result += text.mid(from);
	return result;
}

// Rewrites every color literal of the shipped palette through `replacements`
// and the named colors through `keys`, and hands the result to the running
// theme. Empty maps put the shipped palette back untouched.
void ApplyWith(
		const base::flat_map<QString, QString> &replacements,
		const base::flat_map<QString, QString> &keys = {}) {
	auto file = QFile(PalettePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	auto text = QString::fromLatin1(file.readAll());

	// Alpha is written as two more digits after the color, so the match
	// keeps whatever follows the six it replaces.
	static const auto finder = QRegularExpression(
		u"#([0-9A-Fa-f]{6})"_q);
	auto result = QString();
	result.reserve(text.size());
	auto from = qsizetype(0);
	auto match = QRegularExpressionMatch();
	while ((match = finder.match(text, from)).hasMatch()) {
		result += text.mid(from, match.capturedStart() - from);
		const auto found = match.captured(1).toUpper();
		const auto i = replacements.find(found);
		result += u"#"_q + ((i != replacements.end()) ? i->second : found);
		from = match.capturedEnd();
	}
	result += text.mid(from);
	result = WithKeys(result, keys);

	if (!Window::Theme::ApplyEditedPalette(result.toLatin1())) {
		return;
	}

	// Our default background is a gradient of palette colors, so it has to
	// be rebuilt from the palette that was just applied. A background the
	// user chose themselves is left exactly as it is.
	const auto background = Window::Theme::Background();
	if (Data::IsDefaultWallPaper(background->paper())) {
		background->set(Data::DefaultWallPaper());
	}
}

} // namespace

void ApplyCustomPalette(bool force) {
	auto &settings = AyuSettings::getInstance();
	if (settings.clientTheme() < 1) {
		// Someone else's theme is on; there is nothing of ours to recolor.
		return;
	}

	auto replacements = base::flat_map<QString, QString>();
	auto keys = base::flat_map<QString, QString>();
	for (const auto &slot : PaletteSlots()) {
		const auto picked = Normalized(settings.paletteOverride(slot.id));
		if (picked.isEmpty()) {
			continue;
		} else if (slot.keys.empty()) {
			if (picked != slot.base) {
				replacements.emplace(slot.base, picked);
			}
			continue;
		}
		// A picked color is written even when it equals the base: the
		// window background may have moved the other places that share
		// it, and this part was asked to stay as picked.
		const auto color = QColor(u"#"_q + picked);
		for (const auto &[key, shift] : slot.keys) {
			const auto shade = Shade(color, shift);
			keys[key] = shade.name(QColor::HexRgb).mid(1).toUpper();
		}
	}
	if (replacements.empty() && keys.empty() && !force) {
		return;
	}
	ApplyWith(replacements, keys);
}

void ResetToShippedPalette() {
	if (AyuSettings::getInstance().clientTheme() < 1) {
		return;
	}
	ApplyWith(base::flat_map<QString, QString>());
}

} // namespace HoursGram::Customization
