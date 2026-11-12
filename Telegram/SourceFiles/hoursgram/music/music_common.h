/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

namespace HoursGram::Music {

// The keys of this client have no translations on the language servers,
// so the music screens carry their English and Russian text themselves,
// as the settings page and the side bar do.
[[nodiscard]] bool UsesRussian();
[[nodiscard]] QString Tr(const char *english, const char *russian);

[[nodiscard]] QString TrackTitle(not_null<DocumentData*> document);
[[nodiscard]] QString TrackPerformer(not_null<DocumentData*> document);

// Lower case letters and digits only, so that "Song (feat. X)" and
// "song feat x" compare as the same thing.
[[nodiscard]] QString NormalizeForMatch(const QString &text);

} // namespace HoursGram::Music
