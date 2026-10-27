/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_lyrics.h"

#include <QtCore/QRegularExpression>

namespace HoursGram::Music {
namespace {

constexpr auto kMaxLines = 10000;

// Matches a timing tag body like "01:02", "01:02.75" or "01:02:75".
[[nodiscard]] const QRegularExpression &TimeTagExpression() {
	static const auto result = QRegularExpression(
		u"^(\\d{1,3}):([0-5]?\\d)(?:[.:](\\d{1,3}))?$"_q);
	return result;
}

// Matches the per-word timings of enhanced LRC, which we drop.
[[nodiscard]] const QRegularExpression &WordTagExpression() {
	static const auto result = QRegularExpression(
		u"<\\d{1,3}:[0-5]?\\d(?:[.:]\\d{1,3})?>"_q);
	return result;
}

[[nodiscard]] std::optional<crl::time> ParseTimeTag(const QString &tag) {
	const auto match = TimeTagExpression().match(tag);
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	const auto minutes = match.captured(1).toLongLong();
	const auto seconds = match.captured(2).toLongLong();
	auto fraction = match.captured(3);
	while (fraction.size() < 3) {
		fraction.append('0');
	}
	return crl::time(minutes * 60000 + seconds * 1000 + fraction.toLongLong());
}

// Tags that carry information we either store or knowingly ignore. Anything
// else in square brackets is treated as a part of the line itself.
[[nodiscard]] bool KnownMetaTag(const QString &name) {
	const auto list = std::array{
		u"al"_q,
		u"au"_q,
		u"by"_q,
		u"length"_q,
		u"re"_q,
		u"tool"_q,
		u"ve"_q,
	};
	for (const auto &entry : list) {
		if (name == entry) {
			return true;
		}
	}
	return false;
}

} // namespace

bool Lyrics::synced() const {
	for (const auto &line : lines) {
		if (line.at >= 0) {
			return true;
		}
	}
	return false;
}

int Lyrics::lineAt(crl::time position) const {
	auto result = -1;
	for (auto i = 0, count = int(lines.size()); i != count; ++i) {
		const auto at = lines[i].at;
		if (at < 0) {
			continue;
		} else if (at > position) {
			break;
		}
		result = i;
	}
	return result;
}

Lyrics ParseLyrics(const QString &text) {
	auto result = Lyrics();
	auto entries = std::vector<LyricsLine>();
	auto offset = crl::time(0);
	auto anyTimed = false;
	for (const auto &raw : text.split('\n')) {
		auto line = raw;
		line.remove('\r');

		auto times = std::vector<crl::time>();
		auto position = 0;
		while (position < line.size() && line[position] == '[') {
			const auto close = line.indexOf(']', position + 1);
			if (close < 0) {
				break;
			}
			const auto tag = line.mid(
				position + 1,
				close - position - 1).trimmed();
			if (const auto parsed = ParseTimeTag(tag)) {
				times.push_back(*parsed);
			} else {
				const auto colon = tag.indexOf(':');
				if (colon <= 0) {
					break;
				}
				const auto name = tag.left(colon).trimmed().toLower();
				const auto value = tag.mid(colon + 1).trimmed();
				if (name == u"ti"_q) {
					result.title = value;
				} else if (name == u"ar"_q) {
					result.artist = value;
				} else if (name == u"offset"_q) {
					offset = value.toLongLong();
				} else if (!KnownMetaTag(name)) {
					break;
				}
			}
			position = close + 1;
		}

		auto content = line.mid(position);
		content.remove(WordTagExpression());
		content = content.trimmed();
		if (times.empty()) {
			if (!content.isEmpty()) {
				entries.push_back({ .text = content });
			}
		} else {
			anyTimed = true;
			for (const auto at : times) {
				entries.push_back({ .at = at, .text = content });
			}
		}
		if (int(entries.size()) >= kMaxLines) {
			break;
		}
	}

	if (!anyTimed) {
		result.lines = std::move(entries);
		return result;
	}

	// A timed file mixes in untimed lines only as a header, which the
	// metadata tags already cover, so the timed lines alone are kept.
	auto timed = std::vector<LyricsLine>();
	timed.reserve(entries.size());
	for (auto &entry : entries) {
		if (entry.at >= 0) {
			entry.at = std::max(entry.at - offset, crl::time(0));
			timed.push_back(std::move(entry));
		}
	}
	std::stable_sort(begin(timed), end(timed), [](
			const LyricsLine &a,
			const LyricsLine &b) {
		return a.at < b.at;
	});
	result.lines = std::move(timed);
	return result;
}

Lyrics ParseLyrics(const QByteArray &bytes) {
	// Invalid UTF-8 sequences decode to the replacement character. Lyrics
	// files written before UTF-8 became the norm are saved in the local
	// 8-bit encoding, so that is the fallback.
	auto text = QString::fromUtf8(bytes);
	if (text.contains(QChar::ReplacementCharacter)) {
		text = QString::fromLocal8Bit(bytes);
	}
	return ParseLyrics(text);
}

} // namespace HoursGram::Music
