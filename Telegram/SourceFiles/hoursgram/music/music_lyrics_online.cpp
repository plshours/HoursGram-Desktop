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

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace HoursGram::Music {
namespace {

constexpr auto kMaxResponseSize = 1024 * 1024;

// A public database of synchronized lyrics with an open API that needs no
// key and asks callers to name themselves in the user agent.
constexpr auto kApiUrl = "https://lrclib.net/api/";
constexpr auto kUserAgent = "HoursGram Desktop "
	"(https://github.com/sixseven67123/HoursGram-Desktop)";

[[nodiscard]] QNetworkAccessManager &Manager() {
	static auto result = QNetworkAccessManager();
	return result;
}

// A track is looked up once per run, whether or not anything was found, so
// that skipping back and forth over a track without lyrics stays quiet.
[[nodiscard]] base::flat_set<DocumentId> &Requested() {
	static auto result = base::flat_set<DocumentId>();
	return result;
}

[[nodiscard]] QNetworkRequest MakeRequest(
		const QString &method,
		const QUrlQuery &query) {
	auto url = QUrl(QString::fromUtf8(kApiUrl) + method);
	url.setQuery(query);
	auto result = QNetworkRequest(url);
	result.setRawHeader("User-Agent", kUserAgent);
	result.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	return result;
}

// Prefers the timed lyrics, so that the view can follow the playback, and
// falls back to the plain ones, which still show up line by line.
[[nodiscard]] QString PickText(const QJsonObject &entry) {
	const auto synced = entry.value(u"syncedLyrics"_q).toString();
	return synced.trimmed().isEmpty()
		? entry.value(u"plainLyrics"_q).toString()
		: synced;
}

[[nodiscard]] QString ParseResponse(const QByteArray &bytes) {
	if (bytes.isEmpty() || bytes.size() > kMaxResponseSize) {
		return QString();
	}
	const auto parsed = QJsonDocument::fromJson(bytes);
	if (parsed.isObject()) {
		return PickText(parsed.object());
	} else if (!parsed.isArray()) {
		return QString();
	}
	for (const auto &entry : parsed.array()) {
		if (!entry.isObject()) {
			continue;
		}
		const auto text = PickText(entry.toObject());
		if (!text.trimmed().isEmpty()) {
			return text;
		}
	}
	return QString();
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
	if (title.isEmpty() || performer.isEmpty()) {
		return;
	}
	const auto id = document->id;
	if (!Requested().emplace(id).second) {
		return;
	}
	const auto seconds = int(document->duration() / 1000);

	const auto handle = [=](const QByteArray &bytes) {
		const auto text = ParseResponse(bytes);
		if (text.trimmed().isEmpty()) {
			return false;
		}
		auto lyrics = ParseLyrics(text);
		if (lyrics.empty()) {
			return false;
		}
		CacheLyrics(id, text.toUtf8());
		if (done) {
			done(std::move(lyrics));
		}
		return true;
	};

	// The exact match wants the length of the track as well; when it finds
	// nothing the search by name is tried, which is the looser of the two.
	const auto search = [=] {
		auto query = QUrlQuery();
		query.addQueryItem(u"track_name"_q, title);
		query.addQueryItem(u"artist_name"_q, performer);
		const auto reply = Manager().get(MakeRequest(u"search"_q, query));
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			if (reply->error() == QNetworkReply::NoError) {
				handle(reply->readAll());
			}
			reply->deleteLater();
		});
	};

	auto query = QUrlQuery();
	query.addQueryItem(u"track_name"_q, title);
	query.addQueryItem(u"artist_name"_q, performer);
	if (seconds > 0) {
		query.addQueryItem(u"duration"_q, QString::number(seconds));
	}
	const auto reply = Manager().get(MakeRequest(u"get"_q, query));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto failed = (reply->error() != QNetworkReply::NoError)
			|| !handle(reply->readAll());
		reply->deleteLater();
		if (failed) {
			search();
		}
	});
}

} // namespace HoursGram::Music
