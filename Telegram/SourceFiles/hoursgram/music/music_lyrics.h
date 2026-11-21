/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HoursGram::Music {

// A word of a line sung at its own time, for lyrics that fill word by word
// as they are sung. `duration` is -1 when only the start is known, then the
// word lasts until the next one starts.
struct LyricsWord {
	crl::time at = -1;
	crl::time duration = -1;
	QString text;
};

// A single lyrics line. `at` is the playback position the line starts at,
// in milliseconds, or -1 when the source carried no timing for it. `words`
// put together give `text` exactly, with their spaces; `translation` is the
// line in another language, shown under it.
struct LyricsLine {
	crl::time at = -1;
	QString text;
	std::vector<LyricsWord> words;
	QString translation;

	// How much of `text` is sung at `position`, in characters, fractions
	// counting for the word being sung. -1 when the line has no words.
	[[nodiscard]] float64 sungAt(crl::time position) const;

	// A line all in brackets is sung in the background, by other voices.
	[[nodiscard]] bool background() const;
};

struct Lyrics {
	QString title;
	QString artist;
	std::vector<LyricsLine> lines;

	[[nodiscard]] bool empty() const {
		return lines.empty();
	}

	// True when at least one line carries a timing, so the view can
	// highlight and follow the playback position.
	[[nodiscard]] bool synced() const;

	// Index of the line that should be highlighted at `position`
	// milliseconds, or -1 before the first timed line.
	[[nodiscard]] int lineAt(crl::time position) const;
};

// Parses the LRC format: timing tags like [01:02.75] in front of a line,
// several tags for a repeated line, metadata tags ([ti:], [ar:], [offset:])
// and enhanced word timings like <01:02.75>, which become the words of the
// line. A line tagged [tr:01:02.75] is the translation of the line at that
// time. Text without any timing tag is kept as untimed lines, so plain
// lyrics files still show up.
[[nodiscard]] Lyrics ParseLyrics(const QString &text);

// Decodes bytes read from a file: honours a byte order mark, falls back to
// UTF-8 and then to the local 8-bit encoding, which older .lrc files use.
[[nodiscard]] Lyrics ParseLyrics(const QByteArray &bytes);

// Writes lyrics back as LRC, with the word timings and the translations, so
// that what was found keeps all of it on disk.
[[nodiscard]] QString SerializeLyrics(const Lyrics &lyrics);

} // namespace HoursGram::Music
