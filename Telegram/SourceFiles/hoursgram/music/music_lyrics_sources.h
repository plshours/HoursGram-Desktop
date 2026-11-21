// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 39e5d90b
// Mokagram: тексты песен через открытый сервис LRCLIB (с временными метками).
//
// HoursGram: taken from Telegram/SourceFiles/mokagram/music/mokagram_lyrics.h
// of mokaGram and changed: moved to the HoursGram::Music::Sources namespace,
// the artwork lookup and the Genius source left out, the in-memory cache left
// to the caller, which keeps found lyrics on disk.
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <vector>

namespace HoursGram::Music::Sources {

// HoursGram: a word of a line with its own timing, from the KRC lyrics of
// Kugou, so the line fills word by word as it is sung.
struct LyricWord {
	qint64 timeMs = 0;
	qint64 durationMs = -1;
	QString text;
};

struct LyricLine {
	qint64 timeMs = 0;
	QString text;
	std::vector<LyricWord> words;
};

struct Lyrics {
	std::vector<LyricLine> lines;
	bool synced = false;
	bool found = false;
	bool unavailable = false;
	bool words = false;
};

// Mokagram: LRCLIB first, picking the variant closest in length (speed up
// and slowed versions get their timings stretched), then Kugou and NetEase,
// then a search by the title alone. `done` runs on the main thread.
void FetchLyrics(
	const QString &title,
	const QString &artist,
	qint64 durationMs,
	Fn<void(Lyrics)> done);

// HoursGram: lyrics timed by lines get their words timed too when Kugou has
// the same song in KRC. `done` gets the lyrics with word timings, or lyrics
// with `found` false when there is nothing better than `base`.
void FetchWordTimings(
	const QString &title,
	const QString &artist,
	qint64 durationMs,
	const Lyrics &base,
	Fn<void(Lyrics)> done);

// Название без хвостов вроде "(Official Video)" и без имени исполнителя в начале.
[[nodiscard]] QString CleanTitle(const QString &title, const QString &artist);

} // namespace HoursGram::Music::Sources
