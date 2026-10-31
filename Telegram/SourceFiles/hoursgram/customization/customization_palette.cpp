/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/customization_palette.h"

#include "ayu/ayu_settings.h"
#include "data/data_wall_paper.h"
#include "lang_auto.h"
#include "window/themes/window_theme.h"

#include "base/flat_map.h"
#include "base/flat_set.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>

#include <algorithm>
#include <map>

namespace HoursGram::Customization {
namespace {

constexpr auto kKeyPrefix = "key/";
constexpr auto kMaxReferenceDepth = 32;

// The palette that is shipped for the theme that is on: HorusGram, or the
// dark or the light look of Telegram for macOS.
[[nodiscard]] QString PalettePath() {
	switch (AyuSettings::getInstance().clientTheme()) {
	case 2: return u":/gui/hoursgram-macos-dark.tdesktop-palette"_q;
	case 3: return u":/gui/hoursgram-macos.tdesktop-palette"_q;
	}
	return u":/gui/hoursgram.tdesktop-palette"_q;
}

// Every theme keeps its picks under ids of its own, so a color picked for
// one is never laid over another.
[[nodiscard]] QString ThemePrefix() {
	switch (AyuSettings::getInstance().clientTheme()) {
	case 2: return u"macDark/"_q;
	case 3: return u"macLight/"_q;
	}
	return QString();
}

// Read once per theme: the resources do not change while the app runs,
// and every pick would otherwise read and decode the whole file again.
[[nodiscard]] const QString &ShippedText() {
	static auto cache = std::map<QString, QString>();
	const auto path = PalettePath();
	const auto i = cache.find(path);
	if (i != cache.end()) {
		return i->second;
	}
	auto file = QFile(path);
	auto text = file.open(QIODevice::ReadOnly)
		? QString::fromLatin1(file.readAll())
		: QString();
	return cache.emplace(path, std::move(text)).first->second;
}

// "RRGGBB" or "RRGGBBAA", upper case, without the leading '#'.
[[nodiscard]] QString Normalized(QString color) {
	const auto trimmed = color.trimmed();
	static const auto check = QRegularExpression(
		u"^#?([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$"_q);
	if (!check.match(trimmed).hasMatch()) {
		return QString();
	}
	return trimmed.startsWith(u'#')
		? trimmed.mid(1).toUpper()
		: trimmed.toUpper();
}

// A shade keeps its distance from the color it was derived from: if the
// background goes from #343641 to #0C0C0C, its hover shade #3D404C goes
// to #151618 and not stays where it was.
[[nodiscard]] QString Shifted(
		const QString &follower,
		const QString &base,
		const QString &picked) {
	const auto from = QColor(u"#"_q + base);
	const auto to = QColor(u"#"_q + picked.left(6));
	const auto shade = QColor(u"#"_q + follower);
	const auto shift = [](int value, int delta) {
		return std::clamp(value + delta, 0, 255);
	};
	const auto result = QColor(
		shift(shade.red(), to.red() - from.red()),
		shift(shade.green(), to.green() - from.green()),
		shift(shade.blue(), to.blue() - from.blue()));
	return result.name(QColor::HexRgb).mid(1).toUpper();
}

[[nodiscard]] base::flat_map<QString, QString> SlotReplacements() {
	const auto &settings = AyuSettings::getInstance();
	auto result = base::flat_map<QString, QString>();
	auto shades = base::flat_map<QString, QString>();
	for (const auto &slot : PaletteSlots()) {
		const auto picked = Normalized(settings.paletteOverride(slot.id));
		if (picked.isEmpty() || picked.left(6) == slot.base) {
			continue;
		}
		// Only six digits are swapped; the alpha written after them in the
		// palette stays as it was.
		result.emplace(slot.base, picked.left(6));
		for (const auto &follower : slot.followers) {
			shades.emplace(follower, Shifted(follower, slot.base, picked));
		}
	}
	// A color that is a slot of its own is never moved as someone's shade.
	for (const auto &[follower, shifted] : shades) {
		if (result.find(follower) == result.end()) {
			result.emplace(follower, shifted);
		}
	}
	return result;
}

[[nodiscard]] base::flat_map<QString, QString> KeyOverrides() {
	const auto prefix = KeyOverrideId(QString());
	auto result = base::flat_map<QString, QString>();
	for (const auto &[id, color] : AyuSettings::getInstance().paletteOverrides()) {
		if (!id.startsWith(prefix)) {
			continue;
		}
		const auto normalized = Normalized(color);
		if (!normalized.isEmpty()) {
			result.emplace(id.mid(prefix.size()), normalized);
		}
	}
	return result;
}

// Rewrites every color literal of the palette through `replacements`.
[[nodiscard]] QString ReplaceLiterals(
		const QString &text,
		const base::flat_map<QString, QString> &replacements) {
	if (replacements.empty()) {
		return text;
	}
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
	return result;
}

[[nodiscard]] const QRegularExpression &LineFinder() {
	static const auto result = QRegularExpression(
		u"^(\\s*)([A-Za-z_][A-Za-z0-9_]*)(\\s*:\\s*)([^;]*)(;.*)?$"_q);
	return result;
}

// Gives the keys their own colors. A key the palette does not define is
// added at the end, so a palette that leaves it to the default still gets
// what was picked.
[[nodiscard]] QString ReplaceKeys(
		const QString &text,
		const base::flat_map<QString, QString> &overrides) {
	if (overrides.empty()) {
		return text;
	}
	auto lines = text.split(u'\n');
	auto applied = base::flat_set<QString>();
	for (auto &line : lines) {
		const auto carriage = line.endsWith(u'\r');
		const auto body = carriage ? line.chopped(1) : line;
		const auto match = LineFinder().match(body);
		if (!match.hasMatch()) {
			continue;
		}
		const auto key = match.captured(2);
		const auto i = overrides.find(key);
		if (i == overrides.end()) {
			continue;
		}
		applied.emplace(key);
		line = match.captured(1)
			+ key
			+ match.captured(3)
			+ u"#"_q + i->second
			+ (match.captured(5).isEmpty() ? u";"_q : match.captured(5))
			+ (carriage ? u"\r"_q : QString());
	}
	auto result = lines.join(u'\n');
	for (const auto &[key, color] : overrides) {
		if (applied.find(key) == applied.end()) {
			result += u"\n"_q + key + u": #"_q + color + u";"_q;
		}
	}
	return result;
}

[[nodiscard]] QString ComposedText() {
	return ReplaceKeys(
		ReplaceLiterals(ShippedText(), SlotReplacements()),
		KeyOverrides());
}

[[nodiscard]] base::flat_map<QString, QString> ParseValues(
		const QString &text) {
	auto result = base::flat_map<QString, QString>();
	for (const auto &line : text.split(u'\n')) {
		const auto match = LineFinder().match(line.trimmed());
		if (match.hasMatch()) {
			result[match.captured(2)] = match.captured(4).trimmed();
		}
	}
	return result;
}

// Hands a ready palette to the running theme.
void ApplyText(const QString &text) {
	if (text.isEmpty()
		|| !Window::Theme::ApplyEditedPalette(text.toLatin1())) {
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

const std::vector<PaletteSlot> &PaletteSlots() {
	static const auto macDark = std::vector<PaletteSlot>{
		{ u"macDark/windowBg"_q, u"18222D"_q, {}, [] {
			return tr::hoursgram_ColorWindowBg();
		} },
		{ u"macDark/surface"_q, u"1E2C3B"_q, {}, [] {
			return tr::hoursgram_ColorModalBg();
		} },
		{ u"macDark/accent"_q, u"2EA6FF"_q, {}, [] {
			return tr::hoursgram_ColorAccent();
		} },
		{ u"macDark/accentLight"_q, u"62BCF9"_q, {}, [] {
			return tr::hoursgram_ColorAccentLight();
		} },
		{ u"macDark/subtitle"_q, u"B1C3D5"_q, {}, [] {
			return tr::hoursgram_ColorSubtitle();
		} },
	};
	static const auto macLight = std::vector<PaletteSlot>{
		{ u"macLight/surface"_q, u"F4F4F4"_q, {}, [] {
			return tr::hoursgram_ColorModalBg();
		} },
		{ u"macLight/accent"_q, u"2481CC"_q, {}, [] {
			return tr::hoursgram_ColorAccent();
		} },
		{ u"macLight/accentLight"_q, u"4BA3E2"_q, {}, [] {
			return tr::hoursgram_ColorAccentLight();
		} },
		{ u"macLight/subtitle"_q, u"999999"_q, {}, [] {
			return tr::hoursgram_ColorSubtitle();
		} },
	};
	const auto theme = AyuSettings::getInstance().clientTheme();
	if (theme == 2) {
		return macDark;
	} else if (theme == 3) {
		return macLight;
	}
	static const auto result = std::vector<PaletteSlot>{
		{
			u"windowBg"_q,
			u"343641"_q,
			{ u"3D404C"_q, u"464855"_q, u"46485B"_q, u"4F5268"_q },
			[] { return tr::hoursgram_ColorWindowBg(); },
		},
		{ u"modalBg"_q, u"282A34"_q, {}, [] {
			return tr::hoursgram_ColorModalBg();
		} },
		{
			u"accent"_q,
			u"8993D8"_q,
			{
				u"717CC1"_q,
				u"7B86CF"_q,
				u"6E74A0"_q,
				u"565B7B"_q,
				u"505991"_q,
			},
			[] { return tr::hoursgram_ColorAccent(); },
		},
		{ u"accentLight"_q, u"AAB2ED"_q, {}, [] {
			return tr::hoursgram_ColorAccentLight();
		} },
		{ u"subtitle"_q, u"B1B2B7"_q, { u"626875"_q }, [] {
			return tr::hoursgram_ColorSubtitle();
		} },
	};
	return result;
}

const std::vector<PaletteGroup> &PaletteGroups() {
	using Title = Fn<rpl::producer<QString>()>;
	const auto key = [](const char *name, Title title) {
		return PaletteKey{ QString::fromLatin1(name), std::move(title) };
	};
	const auto avatar = [](int index) {
		return PaletteKey{
			u"historyPeer%1UserpicBg"_q.arg(index),
			[=] {
				return tr::hoursgram_PaletteAvatarColor(
					lt_index,
					rpl::single(QString::number(index)));
			},
		};
	};
	static const auto result = std::vector<PaletteGroup>{
		{ u"in"_q, [] { return tr::hoursgram_PaletteGroupIn(); }, {
			key("msgInBg", [] { return tr::hoursgram_PaletteBubble(); }),
			key("msgInBgSelected", [] {
				return tr::hoursgram_PaletteBubbleSelected();
			}),
			key("msgInShadow", [] { return tr::hoursgram_PaletteShadow(); }),
			key("historyTextInFg", [] { return tr::hoursgram_PaletteText(); }),
			key("msgInDateFg", [] { return tr::hoursgram_PaletteTime(); }),
			key("historyLinkInFg", [] {
				return tr::hoursgram_PaletteLinks();
			}),
			key("msgInServiceFg", [] {
				return tr::hoursgram_PaletteNames();
			}),
			key("msgInReplyBarColor", [] {
				return tr::hoursgram_PaletteReplyBar();
			}),
			key("msgFileInBg", [] { return tr::hoursgram_PaletteFile(); }),
			key("msgWaveformInActive", [] {
				return tr::hoursgram_PaletteWaveform();
			}),
		} },
		{ u"out"_q, [] { return tr::hoursgram_PaletteGroupOut(); }, {
			key("msgOutBg", [] { return tr::hoursgram_PaletteBubble(); }),
			key("msgOutBgSelected", [] {
				return tr::hoursgram_PaletteBubbleSelected();
			}),
			key("msgOutShadow", [] {
				return tr::hoursgram_PaletteShadow();
			}),
			key("historyTextOutFg", [] {
				return tr::hoursgram_PaletteText();
			}),
			key("msgOutDateFg", [] { return tr::hoursgram_PaletteTime(); }),
			key("historyLinkOutFg", [] {
				return tr::hoursgram_PaletteLinks();
			}),
			key("msgOutServiceFg", [] {
				return tr::hoursgram_PaletteNames();
			}),
			key("msgOutReplyBarColor", [] {
				return tr::hoursgram_PaletteReplyBar();
			}),
			key("msgFileOutBg", [] { return tr::hoursgram_PaletteFile(); }),
			key("msgWaveformOutActive", [] {
				return tr::hoursgram_PaletteWaveform();
			}),
			key("historyOutIconFg", [] {
				return tr::hoursgram_PaletteChecks();
			}),
		} },
		{ u"chat"_q, [] { return tr::hoursgram_PaletteGroupChat(); }, {
			key("msgServiceBg", [] {
				return tr::hoursgram_PaletteServiceBg();
			}),
			key("msgServiceFg", [] {
				return tr::hoursgram_PaletteServiceText();
			}),
			key("msgSelectOverlay", [] {
				return tr::hoursgram_PaletteSelection();
			}),
			key("topBarBg", [] { return tr::hoursgram_PaletteTopBar(); }),
			key("historyPinnedBg", [] {
				return tr::hoursgram_PalettePinned();
			}),
			key("historyComposeAreaBg", [] {
				return tr::hoursgram_PaletteInputBg();
			}),
			key("historyComposeAreaFg", [] {
				return tr::hoursgram_PaletteInputText();
			}),
			key("historyComposeIconFg", [] {
				return tr::hoursgram_PaletteInputIcons();
			}),
			key("historySendIconFg", [] {
				return tr::hoursgram_PaletteSend();
			}),
			key("historyReplyBg", [] {
				return tr::hoursgram_PaletteReplyPreview();
			}),
			key("historyUnreadBarBg", [] {
				return tr::hoursgram_PaletteUnreadBar();
			}),
			key("historyToDownBg", [] {
				return tr::hoursgram_PaletteToDown();
			}),
		} },
		{ u"list"_q, [] { return tr::hoursgram_PaletteGroupList(); }, {
			key("dialogsBg", [] { return tr::hoursgram_PaletteListBg(); }),
			key("dialogsBgOver", [] {
				return tr::hoursgram_PaletteListBgOver();
			}),
			key("dialogsBgActive", [] {
				return tr::hoursgram_PaletteListBgActive();
			}),
			key("dialogsNameFg", [] {
				return tr::hoursgram_PaletteListName();
			}),
			key("dialogsTextFg", [] {
				return tr::hoursgram_PaletteListText();
			}),
			key("dialogsDateFg", [] { return tr::hoursgram_PaletteTime(); }),
			key("dialogsUnreadBg", [] {
				return tr::hoursgram_PaletteUnread();
			}),
			key("dialogsUnreadBgMuted", [] {
				return tr::hoursgram_PaletteUnreadMuted();
			}),
			key("dialogsUnreadFg", [] {
				return tr::hoursgram_PaletteUnreadText();
			}),
			key("sideBarBg", [] {
				return tr::hoursgram_PaletteFoldersBg();
			}),
			key("sideBarBgActive", [] {
				return tr::hoursgram_PaletteFoldersActive();
			}),
			key("sideBarTextFg", [] {
				return tr::hoursgram_PaletteFoldersText();
			}),
			key("sideBarIconFg", [] {
				return tr::hoursgram_PaletteFoldersIcons();
			}),
			key("sideBarBadgeBg", [] {
				return tr::hoursgram_PaletteFoldersBadge();
			}),
		} },
		{ u"profile"_q, [] { return tr::hoursgram_PaletteGroupProfile(); }, {
			key("windowBoldFg", [] {
				return tr::hoursgram_PaletteProfileName();
			}),
			key("windowSubTextFg", [] {
				return tr::hoursgram_PaletteProfileStatus();
			}),
			key("windowActiveTextFg", [] {
				return tr::hoursgram_PaletteProfileOnline();
			}),
			key("profileStatusFgOver", [] {
				return tr::hoursgram_PaletteHover();
			}),
			key("profileVerifiedCheckBg", [] {
				return tr::hoursgram_PaletteVerified();
			}),
			key("profileVerifiedCheckFg", [] {
				return tr::hoursgram_PaletteVerifiedCheck();
			}),
			key("profileAdminStartFg", [] {
				return tr::hoursgram_PaletteAdminStar();
			}),
			key("menuIconFg", [] {
				return tr::hoursgram_PaletteProfileIcons();
			}),
			key("mainMenuBg", [] {
				return tr::hoursgram_PaletteMainMenu();
			}),
			key("mainMenuCoverBg", [] {
				return tr::hoursgram_PaletteMainMenuCover();
			}),
			key("mainMenuCoverFg", [] {
				return tr::hoursgram_PaletteMainMenuCoverText();
			}),
			key("historyPeerUserpicFg", [] {
				return tr::hoursgram_PaletteAvatarLetters();
			}),
		} },
		{ u"avatars"_q, [] { return tr::hoursgram_PaletteGroupAvatars(); }, {
			avatar(1),
			avatar(2),
			avatar(3),
			avatar(4),
			avatar(5),
			avatar(6),
			avatar(7),
			avatar(8),
		} },
		{ u"controls"_q, [] {
			return tr::hoursgram_PaletteGroupControls();
		}, {
			key("activeButtonBg", [] {
				return tr::hoursgram_PaletteButton();
			}),
			key("activeButtonBgOver", [] {
				return tr::hoursgram_PaletteButtonOver();
			}),
			key("activeButtonFg", [] {
				return tr::hoursgram_PaletteButtonText();
			}),
			key("lightButtonFg", [] {
				return tr::hoursgram_PaletteFlatButton();
			}),
			key("lightButtonBgOver", [] {
				return tr::hoursgram_PaletteFlatButtonOver();
			}),
			key("attentionButtonFg", [] {
				return tr::hoursgram_PaletteDanger();
			}),
			key("inputBorderFg", [] {
				return tr::hoursgram_PaletteFieldLine();
			}),
			key("activeLineFg", [] {
				return tr::hoursgram_PaletteFieldLineActive();
			}),
			key("filterInputInactiveBg", [] {
				return tr::hoursgram_PaletteSearchField();
			}),
			key("filterInputActiveBg", [] {
				return tr::hoursgram_PaletteSearchFieldActive();
			}),
			key("checkboxFg", [] {
				return tr::hoursgram_PaletteCheckbox();
			}),
			key("sliderBgActive", [] {
				return tr::hoursgram_PaletteSlider();
			}),
			key("sliderBgInactive", [] {
				return tr::hoursgram_PaletteSliderEmpty();
			}),
			key("boxTitleFg", [] {
				return tr::hoursgram_PaletteBoxTitle();
			}),
			key("boxTextFg", [] {
				return tr::hoursgram_PaletteBoxText();
			}),
			key("emojiPanBg", [] {
				return tr::hoursgram_PaletteEmojiPanel();
			}),
		} },
		{ u"window"_q, [] { return tr::hoursgram_PaletteGroupWindow(); }, {
			key("windowBg", [] { return tr::hoursgram_ColorWindowBg(); }),
			key("windowBgOver", [] {
				return tr::hoursgram_PaletteHover();
			}),
			key("windowFg", [] { return tr::hoursgram_PaletteText(); }),
			key("windowSubTextFg", [] {
				return tr::hoursgram_ColorSubtitle();
			}),
			key("windowBoldFg", [] {
				return tr::hoursgram_PaletteBoldText();
			}),
			key("boxBg", [] { return tr::hoursgram_PaletteBoxes(); }),
			key("menuBg", [] { return tr::hoursgram_PaletteMenus(); }),
			key("menuBgOver", [] {
				return tr::hoursgram_PaletteMenusOver();
			}),
			key("titleBg", [] { return tr::hoursgram_PaletteTitleBg(); }),
			key("titleFg", [] { return tr::hoursgram_PaletteTitleText(); }),
			key("scrollBarBg", [] {
				return tr::hoursgram_PaletteScrollBar();
			}),
			key("tooltipBg", [] {
				return tr::hoursgram_PaletteTooltips();
			}),
			key("toastBg", [] { return tr::hoursgram_PaletteToasts(); }),
		} },
	};
	return result;
}

std::vector<QString> PaletteKeys() {
	auto result = std::vector<QString>();
	auto seen = base::flat_set<QString>();
	for (const auto &line : ShippedText().split(u'\n')) {
		const auto match = LineFinder().match(line.trimmed());
		if (match.hasMatch() && seen.emplace(match.captured(2)).second) {
			result.push_back(match.captured(2));
		}
	}
	return result;
}

QString KeyOverrideId(const QString &key) {
	return ThemePrefix() + QString::fromLatin1(kKeyPrefix) + key;
}

bool IsCurrentThemeOverride(const QString &id) {
	const auto prefix = ThemePrefix();
	if (!prefix.isEmpty()) {
		return id.startsWith(prefix);
	}
	return !id.startsWith(u"macDark/"_q) && !id.startsWith(u"macLight/"_q);
}

base::flat_map<QString, QString> CurrentKeyColors() {
	const auto values = ParseValues(ComposedText());
	const auto resolve = [&](QString name) {
		for (auto i = 0; i != kMaxReferenceDepth; ++i) {
			const auto found = values.find(name);
			if (found == values.end()) {
				return QString();
			}
			const auto normalized = Normalized(found->second);
			if (!normalized.isEmpty()) {
				return normalized;
			}
			// A key may be written as another key's name.
			name = found->second;
		}
		return QString();
	};
	auto result = base::flat_map<QString, QString>();
	for (const auto &[key, value] : values) {
		if (const auto color = resolve(key); !color.isEmpty()) {
			result.emplace(key, color);
		}
	}
	return result;
}

QString ColorToHex(const QColor &color) {
	auto result = color.name(QColor::HexRgb).mid(1).toUpper();
	if (color.alpha() < 255) {
		result += u"%1"_q.arg(color.alpha(), 2, 16, QChar('0')).toUpper();
	}
	return result;
}

QColor HexToColor(const QString &hex) {
	const auto normalized = Normalized(hex);
	if (normalized.isEmpty()) {
		return QColor();
	}
	auto result = QColor(u"#"_q + normalized.left(6));
	if (normalized.size() == 8) {
		result.setAlpha(normalized.mid(6).toInt(nullptr, 16));
	}
	return result;
}

void ApplyCustomPalette(bool force) {
	if (AyuSettings::getInstance().clientTheme() < 1) {
		// Someone else's theme is on; there is nothing of ours to recolor.
		return;
	}
	const auto slots = SlotReplacements();
	const auto keys = KeyOverrides();
	if (slots.empty() && keys.empty() && !force) {
		return;
	}
	ApplyText(ReplaceKeys(ReplaceLiterals(ShippedText(), slots), keys));
}

void ResetToShippedPalette() {
	if (AyuSettings::getInstance().clientTheme() < 1) {
		return;
	}
	ApplyText(ShippedText());
}

} // namespace HoursGram::Customization
