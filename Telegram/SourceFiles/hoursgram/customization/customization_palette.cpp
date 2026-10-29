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

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>

namespace HoursGram::Customization {
namespace {

// The palette that is shipped for the theme that is on: HorusGram, or the
// dark or the light look of Telegram for macOS.
[[nodiscard]] QString PalettePath() {
	switch (AyuSettings::getInstance().clientTheme()) {
	case 2: return u":/gui/hoursgram-macos-dark.tdesktop-palette"_q;
	case 3: return u":/gui/hoursgram-macos.tdesktop-palette"_q;
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
	const auto theme = AyuSettings::getInstance().clientTheme();
	if (theme == 2) {
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

// Rewrites every color literal of the shipped palette through `replacements`
// and hands the result to the running theme. An empty map puts the shipped
// palette back untouched.
void ApplyWith(const base::flat_map<QString, QString> &replacements) {
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
	for (const auto &slot : PaletteSlots()) {
		const auto picked = Normalized(settings.paletteOverride(slot.id));
		if (!picked.isEmpty() && picked != slot.base) {
			replacements.emplace(slot.base, picked);
		}
	}
	if (replacements.empty() && !force) {
		return;
	}
	ApplyWith(replacements);
}

void ResetToShippedPalette() {
	if (AyuSettings::getInstance().clientTheme() < 1) {
		return;
	}
	ApplyWith(base::flat_map<QString, QString>());
}

} // namespace HoursGram::Customization
