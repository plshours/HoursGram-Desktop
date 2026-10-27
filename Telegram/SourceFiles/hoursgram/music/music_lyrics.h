/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HoursGram::Music {

// A single lyrics line. `at` is the playback position the line starts at,
// in milliseconds, or -1 when the source carried no timing for it.
struct LyricsLine {
	crl::time at = -1;
	QString text;
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
// and enhanced word timings, which are dropped. Text without any timing tag
// is kept as untimed lines, so plain lyrics files still show up.
[[nodiscard]] Lyrics ParseLyrics(const QString &text);

// Decodes bytes read from a file: honours a byte order mark, falls back to
// UTF-8 and then to the local 8-bit encoding, which older .lrc files use.
[[nodiscard]] Lyrics ParseLyrics(const QByteArray &bytes);

} // namespace HoursGram::Music
