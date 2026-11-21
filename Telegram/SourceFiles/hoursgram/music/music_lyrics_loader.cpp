/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_lyrics_loader.h"

#include "ayu/ayu_settings.h"
#include "core/application.h"
#include "data/data_document.h"
#include "hoursgram/music/music_common.h"
#include "hoursgram/music/music_lyrics_sources.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>

#include <algorithm>

namespace HoursGram::Music {
namespace {

constexpr auto kMaxFileSize = 1024 * 1024;

[[nodiscard]] QString LyricsFolder() {
	return cWorkingDir() + u"tdata/hoursgram_lyrics/"_q;
}

[[nodiscard]] QString CacheFolder() {
	return LyricsFolder() + u"online/"_q;
}

bool WriteFile(const QString &path, const QByteArray &bytes) {
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return (file.write(bytes) == bytes.size());
}

[[nodiscard]] Lyrics ReadFile(const QString &path) {
	if (path.isEmpty()) {
		return {};
	}
	auto file = QFile(path);
	if (!file.exists()
		|| file.size() > kMaxFileSize
		|| !file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return ParseLyrics(file.readAll());
}

// The title and the performer come from the tags of the file, which anyone
// who posts it chooses. Used in a path they must stay a bare file name:
// a separator or a drive colon would let "..\..\" or "C:" reach files
// outside of the folder of the track.
[[nodiscard]] bool SafeFileName(const QString &name) {
	if (name.isEmpty()) {
		return false;
	}
	for (const auto ch : name) {
		if (ch == '/' || ch == '\\' || ch == ':' || ch.unicode() < 0x20) {
			return false;
		}
	}
	return true;
}

// Lyrics files are commonly saved next to the audio file, either under the
// same name or under "performer - title".
[[nodiscard]] std::vector<QString> SiblingPaths(
		not_null<DocumentData*> document) {
	auto result = std::vector<QString>();
	const auto path = document->filepath();
	if (path.isEmpty()) {
		return result;
	}
	const auto info = QFileInfo(path);
	const auto folder = info.absolutePath() + '/';
	result.push_back(folder + info.completeBaseName() + u".lrc"_q);
	if (const auto song = document->song()) {
		const auto title = song->title.trimmed();
		const auto performer = song->performer.trimmed();
		if (SafeFileName(title)) {
			if (SafeFileName(performer)) {
				result.push_back(
					folder + performer + u" - "_q + title + u".lrc"_q);
			}
			result.push_back(folder + title + u".lrc"_q);
		}
	}
	return result;
}

// The folder of lyrics the user chose holds files named after the song,
// "Performer — Title.lrc" or ".txt", the way a lyrics finder saves them.
// The names are matched by their words, so the order of the performer and
// the title, the dash and the punctuation do not matter.
constexpr auto kMaxLibraryFiles = 5000;

[[nodiscard]] QStringList Words(const QString &text) {
	auto normalized = text.toLower();
	normalized.replace(QChar(0x0451), QChar(0x0435)); // yo -> ye
	static const auto other = QRegularExpression(u"[^\\p{L}\\p{N}]+"_q);
	normalized.replace(other, u" "_q);
	return normalized.split(' ', Qt::SkipEmptyParts);
}

// A .txt file of the lyrics finder starts with "Performer — Title" and a
// line naming the source, which are not part of the song.
[[nodiscard]] QString WithoutHeader(const QString &text) {
	auto lines = text.split('\n');
	if (lines.size() > 2) {
		const auto second = lines[1].trimmed();
		if (second.startsWith(u"Источник:"_q)
			|| second.startsWith(u"Source:"_q, Qt::CaseInsensitive)) {
			lines.erase(lines.begin(), lines.begin() + 2);
			while (!lines.isEmpty() && lines.front().trimmed().isEmpty()) {
				lines.pop_front();
			}
		}
	}
	return lines.join('\n');
}

[[nodiscard]] Lyrics ReadLibraryFile(const QFileInfo &info) {
	auto file = QFile(info.absoluteFilePath());
	if (info.size() > kMaxFileSize || !file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto bytes = file.readAll();
	if (info.suffix().compare(u"txt"_q, Qt::CaseInsensitive) == 0) {
		return ParseLyrics(WithoutHeader(QString::fromUtf8(bytes)));
	}
	return ParseLyrics(bytes);
}

[[nodiscard]] Lyrics LoadFromLibrary(not_null<DocumentData*> document) {
	const auto folder = AyuSettings::getInstance().lyricsFolder();
	if (folder.isEmpty()) {
		return {};
	}
	const auto performerText = TrackPerformer(document);
	const auto title = Words(
		Sources::CleanTitle(TrackTitle(document), performerText));
	const auto performer = Words(performerText);
	if (title.isEmpty()) {
		return {};
	}
	struct Candidate {
		QFileInfo info;
		double score = 0.;
	};
	auto candidates = std::vector<Candidate>();
	const auto entries = QDir(folder).entryInfoList(
		{ u"*.lrc"_q, u"*.txt"_q },
		QDir::Files | QDir::Readable,
		QDir::Name);
	auto checked = 0;
	for (const auto &info : entries) {
		if (++checked > kMaxLibraryFiles) {
			break;
		}
		const auto name = Words(info.completeBaseName());
		const auto hasAll = std::all_of(
			title.begin(),
			title.end(),
			[&](const QString &word) { return name.contains(word); });
		if (!hasAll) {
			continue;
		}
		auto matched = 0;
		for (const auto &word : performer) {
			if (name.contains(word)) {
				++matched;
			}
		}
		auto extra = 0;
		for (const auto &word : name) {
			if (!title.contains(word) && !performer.contains(word)) {
				++extra;
			}
		}
		// Without the performer in the name, only a name that is the title
		// and nothing else counts: a common title would match other songs.
		if (!performer.isEmpty()
			&& matched * 2 < performer.size()
			&& extra > 0) {
			continue;
		}
		const auto timed = (info.suffix().compare(
			u"lrc"_q,
			Qt::CaseInsensitive) == 0);
		candidates.push_back({
			info,
			matched * 10. - extra + (timed ? 0.5 : 0.),
		});
	}
	std::sort(candidates.begin(), candidates.end(), [](
			const Candidate &a,
			const Candidate &b) {
		return a.score > b.score;
	});
	for (const auto &candidate : candidates) {
		auto result = ReadLibraryFile(candidate.info);
		if (!result.empty()) {
			return result;
		}
	}
	return {};
}

} // namespace

QString LyricsStorePath(DocumentId id) {
	return LyricsFolder() + QString::number(id) + u".lrc"_q;
}

Lyrics LoadLyrics(not_null<DocumentData*> document) {
	auto result = ReadFile(LyricsStorePath(document->id));
	if (!result.empty()) {
		return result;
	}
	for (const auto &path : SiblingPaths(document)) {
		result = ReadFile(path);
		if (!result.empty()) {
			return result;
		}
	}
	result = LoadFromLibrary(document);
	if (!result.empty()) {
		return result;
	}
	return ReadFile(LyricsCachePath(document->id));
}

QString LyricsCachePath(DocumentId id) {
	return CacheFolder() + QString::number(id) + u".lrc"_q;
}

bool CacheLyrics(DocumentId id, const QByteArray &bytes) {
	if (bytes.isEmpty() || bytes.size() > kMaxFileSize) {
		return false;
	} else if (!QDir().mkpath(CacheFolder())) {
		return false;
	}
	return WriteFile(LyricsCachePath(id), bytes);
}

bool AttachLyrics(DocumentId id, const QByteArray &bytes) {
	if (bytes.isEmpty() || bytes.size() > kMaxFileSize) {
		return false;
	} else if (!QDir().mkpath(LyricsFolder())) {
		return false;
	}
	return WriteFile(LyricsStorePath(id), bytes);
}

void DetachLyrics(DocumentId id) {
	QFile::remove(LyricsStorePath(id));
}

bool LyricsAttached(DocumentId id) {
	return QFile::exists(LyricsStorePath(id));
}

} // namespace HoursGram::Music
