/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_lyrics_loader.h"

#include "core/application.h"
#include "data/data_document.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

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
		if (!title.isEmpty()) {
			if (!performer.isEmpty()) {
				result.push_back(
					folder + performer + u" - "_q + title + u".lrc"_q);
			}
			result.push_back(folder + title + u".lrc"_q);
		}
	}
	return result;
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
