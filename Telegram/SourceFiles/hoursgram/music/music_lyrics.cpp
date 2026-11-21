/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_lyrics.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>

namespace HoursGram::Music {
namespace {

constexpr auto kMaxLines = 10000;

// Matches a timing tag body like "01:02", "01:02.75" or "01:02:75".
[[nodiscard]] const QRegularExpression &TimeTagExpression() {
	static const auto result = QRegularExpression(
		u"^(\\d{1,3}):([0-5]?\\d)(?:[.:](\\d{1,3}))?$"_q);
	return result;
}

// Matches the per-word timings of enhanced LRC.
[[nodiscard]] const QRegularExpression &WordTagExpression() {
	static const auto result = QRegularExpression(
		u"<(\\d{1,3}:[0-5]?\\d(?:[.:]\\d{1,3})?)>"_q);
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

// Splits a line with word timings into its words. The text between two tags
// is the word of the first; a tag at the very end only closes the last word.
// Text before the first tag belongs to the line start.
[[nodiscard]] std::vector<LyricsWord> ParseWords(
		const QString &content,
		crl::time lineAt) {
	auto result = std::vector<LyricsWord>();
	auto it = WordTagExpression().globalMatch(content);
	if (!it.hasNext()) {
		return result;
	}
	auto tags = std::vector<std::pair<crl::time, QRegularExpressionMatch>>();
	while (it.hasNext()) {
		const auto match = it.next();
		if (const auto at = ParseTimeTag(match.captured(1))) {
			tags.emplace_back(*at, match);
		}
	}
	if (tags.empty()) {
		return result;
	}
	const auto before = content.left(tags.front().second.capturedStart());
	if (!before.trimmed().isEmpty()) {
		result.push_back({ .at = lineAt, .text = before });
	}
	for (auto i = 0, count = int(tags.size()); i != count; ++i) {
		const auto from = tags[i].second.capturedEnd();
		const auto till = (i + 1 < count)
			? tags[i + 1].second.capturedStart()
			: content.size();
		const auto text = content.mid(from, till - from);
		if (text.isEmpty()) {
			// A closing tag: the end of the word before it.
			if (!result.empty() && result.back().duration < 0) {
				result.back().duration = std::max(
					tags[i].first - result.back().at,
					crl::time(0));
			}
			continue;
		}
		if (!result.empty() && result.back().duration < 0) {
			result.back().duration = std::max(
				tags[i].first - result.back().at,
				crl::time(0));
		}
		result.push_back({ .at = tags[i].first, .text = text });
	}
	// The words give the text of the line: no spaces around it.
	while (!result.empty() && result.front().text.trimmed().isEmpty()) {
		result.erase(result.begin());
	}
	while (!result.empty() && result.back().text.trimmed().isEmpty()) {
		result.pop_back();
	}
	if (!result.empty()) {
		auto &first = result.front().text;
		while (!first.isEmpty() && first.front().isSpace()) {
			first.remove(0, 1);
		}
		auto &last = result.back().text;
		while (!last.isEmpty() && last.back().isSpace()) {
			last.chop(1);
		}
	}
	return result;
}

[[nodiscard]] QString FormatTime(crl::time ms) {
	ms = std::max(ms, crl::time(0));
	return u"%1:%2.%3"_q
		.arg(ms / 60000, 2, 10, QChar('0'))
		.arg((ms / 1000) % 60, 2, 10, QChar('0'))
		.arg((ms % 1000) / 10, 2, 10, QChar('0'));
}

} // namespace

float64 LyricsLine::sungAt(crl::time position) const {
	if (words.empty()) {
		return -1.;
	}
	auto result = 0.;
	for (auto i = 0, count = int(words.size()); i != count; ++i) {
		const auto &word = words[i];
		const auto length = float64(word.text.size());
		if (word.at < 0 || position < word.at) {
			break;
		}
		const auto end = (word.duration >= 0)
			? (word.at + word.duration)
			: (i + 1 < count && words[i + 1].at >= 0)
			? words[i + 1].at
			: (word.at + 400);
		if (position >= end || end <= word.at) {
			result += length;
			continue;
		}
		result += length * float64(position - word.at) / (end - word.at);
		break;
	}
	return result;
}

bool LyricsLine::background() const {
	const auto trimmed = text.trimmed();
	return (trimmed.size() > 2)
		&& (trimmed.front() == '(')
		&& (trimmed.back() == ')')
		&& (trimmed.indexOf(')') == trimmed.size() - 1);
}

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
	auto translations = std::vector<std::pair<crl::time, QString>>();
	auto offset = crl::time(0);
	auto anyTimed = false;
	for (const auto &raw : text.split('\n')) {
		auto line = raw;
		line.remove('\r');

		auto times = std::vector<crl::time>();
		auto translationAt = std::optional<crl::time>();
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
				if (name == u"tr"_q) {
					translationAt = ParseTimeTag(value);
					if (!translationAt) {
						break;
					}
				} else if (name == u"ti"_q) {
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
		if (translationAt) {
			content.remove(WordTagExpression());
			content = content.trimmed();
			if (!content.isEmpty()) {
				translations.emplace_back(*translationAt, content);
			}
			continue;
		}
		// Words timed one by one only make sense for a line sung once.
		auto words = (times.size() == 1)
			? ParseWords(content, times.front())
			: std::vector<LyricsWord>();
		content.remove(WordTagExpression());
		content = content.trimmed();
		if (!words.empty()) {
			auto joined = QString();
			for (const auto &word : words) {
				joined += word.text;
			}
			content = joined;
		}
		if (times.empty()) {
			if (!content.isEmpty()) {
				entries.push_back({ .text = content });
			}
		} else {
			anyTimed = true;
			for (const auto at : times) {
				entries.push_back({
					.at = at,
					.text = content,
					.words = words,
				});
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
			for (auto &word : entry.words) {
				word.at = std::max(word.at - offset, crl::time(0));
			}
			timed.push_back(std::move(entry));
		}
	}
	std::stable_sort(begin(timed), end(timed), [](
			const LyricsLine &a,
			const LyricsLine &b) {
		return a.at < b.at;
	});
	// A translation goes to the line starting at its time, or to the
	// closest one when the times were rounded differently.
	for (auto &[at, text] : translations) {
		at = std::max(at - offset, crl::time(0));
		auto best = (LyricsLine*)nullptr;
		auto distance = crl::time(300);
		for (auto &line : timed) {
			const auto d = std::abs(line.at - at);
			if (d <= distance && line.translation.isEmpty()) {
				best = &line;
				distance = d;
			}
		}
		if (best) {
			best->translation = text;
		}
	}
	result.lines = std::move(timed);
	return result;
}

QString SerializeLyrics(const Lyrics &lyrics) {
	auto result = QStringList();
	const auto synced = lyrics.synced();
	for (const auto &line : lyrics.lines) {
		if (!synced || line.at < 0) {
			result.push_back(line.text);
			continue;
		}
		auto text = QString();
		if (line.words.empty()) {
			text = line.text;
		} else {
			for (const auto &word : line.words) {
				text += u"<"_q + FormatTime(word.at) + u">"_q + word.text;
			}
			const auto &last = line.words.back();
			if (last.duration >= 0) {
				text += u"<"_q + FormatTime(last.at + last.duration) + u">"_q;
			}
		}
		result.push_back(u"["_q + FormatTime(line.at) + u"]"_q + text);
		if (!line.translation.isEmpty()) {
			result.push_back(u"[tr:"_q
				+ FormatTime(line.at)
				+ u"]"_q
				+ line.translation);
		}
	}
	return result.join('\n');
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
