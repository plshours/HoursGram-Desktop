/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_lyrics_online.h"

#include "base/flat_set.h"
#include "data/data_document.h"
#include "hoursgram/music/music_lyrics_loader.h"
#include "hoursgram/music/music_lyrics_sources.h"

#include <QtCore/QStringList>

namespace HoursGram::Music {
namespace {

// A track is looked up once per run, whether or not anything was found, so
// that skipping back and forth over a track without lyrics stays quiet.
[[nodiscard]] base::flat_set<DocumentId> &Requested() {
	static auto result = base::flat_set<DocumentId>();
	return result;
}

[[nodiscard]] QString Stamp(qint64 ms) {
	ms = std::max(ms, qint64(0));
	return u"%1:%2.%3"_q
		.arg(ms / 60000, 2, 10, QChar('0'))
		.arg((ms / 1000) % 60, 2, 10, QChar('0'))
		.arg((ms % 1000) / 10, 2, 10, QChar('0'));
}

// Timed lines become an LRC file, plain ones a text file, which the parser
// of lyrics files reads either way. Words timed one by one are written as
// enhanced LRC, <mm:ss.xx> in front of every word.
[[nodiscard]] QString ToLrc(const Sources::Lyrics &found) {
	auto result = QStringList();
	result.reserve(int(found.lines.size()));
	for (const auto &line : found.lines) {
		if (!found.synced) {
			result.push_back(line.text);
			continue;
		}
		auto text = QString();
		if (line.words.empty()) {
			text = line.text;
		} else {
			for (const auto &word : line.words) {
				text += u"<"_q + Stamp(word.timeMs) + u">"_q + word.text;
			}
			const auto &last = line.words.back();
			if (last.durationMs >= 0) {
				text += u"<"_q
					+ Stamp(last.timeMs + last.durationMs)
					+ u">"_q;
			}
		}
		result.push_back(u"["_q + Stamp(line.timeMs) + u"]"_q + text);
	}
	return result.join(QChar('\n'));
}

} // namespace

void SearchLyricsOnline(
		not_null<DocumentData*> document,
		Fn<void(Lyrics)> done) {
	const auto song = document->song();
	if (!song) {
		return;
	}
	const auto title = song->title.trimmed();
	const auto performer = song->performer.trimmed();
	if (title.isEmpty()) {
		return;
	}
	const auto id = document->id;
	if (!Requested().emplace(id).second) {
		return;
	}

	// The lookup of mokaGram: LRCLIB, Kugou and NetEase, picking the variant
	// closest in length. A performer missing from the tags is taken from an
	// "Artist - Title" title. What is found is kept on disk as an LRC file.
	const auto duration = document->duration();
	const auto deliver = [=](const Sources::Lyrics &found) {
		const auto text = ToLrc(found);
		auto lyrics = ParseLyrics(text);
		if (lyrics.empty()) {
			return;
		}
		CacheLyrics(id, text.toUtf8());
		if (done) {
			done(std::move(lyrics));
		}
	};
	Sources::FetchLyrics(
		title,
		performer,
		duration,
		[=](Sources::Lyrics found) {
			if (!found.found || found.lines.empty()) {
				return;
			}
			deliver(found);
			// HoursGram: lines timed one by one are shown at once; when
			// Kugou has the same song with every word timed, the lyrics
			// are replaced with those, so they fill word by word.
			Sources::FetchWordTimings(
				title,
				performer,
				duration,
				found,
				[=](Sources::Lyrics words) {
					if (words.found && !words.lines.empty()) {
						deliver(words);
					}
				});
		});
}

} // namespace HoursGram::Music
