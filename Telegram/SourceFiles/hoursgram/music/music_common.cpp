/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_common.h"

#include "data/data_document.h"
#include "lang/lang_instance.h"

namespace HoursGram::Music {

bool UsesRussian() {
	const auto &lang = Lang::GetInstance();
	return lang.id().startsWith(u"ru"_q) || lang.baseId().startsWith(u"ru"_q);
}

QString Tr(const char *english, const char *russian) {
	return UsesRussian()
		? QString::fromUtf8(russian)
		: QString::fromUtf8(english);
}

QString TrackTitle(not_null<DocumentData*> document) {
	const auto song = document->song();
	if (song && !song->title.trimmed().isEmpty()) {
		return song->title.trimmed();
	}
	const auto name = document->filename();
	const auto dot = name.lastIndexOf('.');
	return (dot > 0) ? name.mid(0, dot) : name;
}

QString TrackPerformer(not_null<DocumentData*> document) {
	const auto song = document->song();
	return (song && !song->performer.trimmed().isEmpty())
		? song->performer.trimmed()
		: Tr("Unknown artist", "Неизвестный исполнитель");
}

QString NormalizeForMatch(const QString &text) {
	auto result = QString();
	result.reserve(text.size());
	for (const auto ch : text.toLower()) {
		if (ch == QChar(0x0451)) {
			result.append(QChar(0x0435));
		} else if (ch.isLetterOrNumber()) {
			result.append(ch);
		}
	}
	return result;
}

} // namespace HoursGram::Music
