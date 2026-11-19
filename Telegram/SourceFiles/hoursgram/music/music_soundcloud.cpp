/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
/*
This file is derived from mokaGram, https://github.com/ecronxy/mokagram
(c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
*/
/*
SoundCloud integration notice.

This module is a plain HTTPS client for the public SoundCloud web API
(api-v2.soundcloud.com) — the same endpoints the official soundcloud.com
web player uses in an ordinary anonymous browser session.

What it does:
- searches tracks for a user-entered query and requests stream URLs
  for the track the user clicks, the same way the web player does;
- plays those streams locally, caching audio and artwork in the
  application cache directory only, like a browser cache does;
- keeps every request inside SoundCloud's own domains (allow-listed
  hosts), with response size limits, timeouts, and a bounded
  concurrent-image queue.

What it does not do:
- no login, no authentication bypass, no private or undocumented
  APIs: the client_id it uses is the public anonymous one embedded in
  the web client's own JavaScript bundles;
- no circumvention of technical protection measures: the HLS media
  playlists it reads reference plain, unencrypted AAC/MP3 segments
  served openly by SoundCloud, nothing is decrypted, and no protected
  or DRM-restricted content is ever handled;
- no bulk scraping or crawling: requests happen only in response to
  the user typing a query or clicking a result, and rate-limit
  responses (HTTP 429) are surfaced as errors, never retried in a
  loop.

"SoundCloud" and related marks are trademarks of SoundCloud Ltd.
This project is not affiliated with, endorsed by, or sponsored by
SoundCloud Ltd.; the mark is used solely to identify the service this
feature connects to. The feature performs no network requests until
the user opens the music search and starts typing a query.
*/
#include "hoursgram/music/music_soundcloud.h"

#include "lang/lang_keys.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>

namespace HoursGram::Music {
namespace {

constexpr auto kApi = "https://api-v2.soundcloud.com";
constexpr auto kHome = "https://soundcloud.com/";
constexpr auto kPageSize = 20;
constexpr auto kMaxAudioBytes = 200 * 1024 * 1024;
constexpr auto kMaxImageBytes = 32 * 1024 * 1024;
constexpr auto kMaxHlsSegments = 512;
constexpr auto kUserAgent =
	"HoursGram/1.0 (Telegram Desktop fork; music search)";
constexpr auto kConcurrentImages = 6;

QNetworkAccessManager &Net() {
	static auto manager = new QNetworkAccessManager();
	return *manager;
}

[[nodiscard]] bool SafeMediaUrl(const QUrl &url) {
	const auto host = url.host();
	return url.isValid()
		&& url.scheme() == u"https"_q
		&& url.userInfo().isEmpty()
		&& url.port(-1) == -1
		&& (host == u"api-v2.soundcloud.com"_q
			|| host == u"api.soundcloud.com"_q
			|| host == u"soundcloud.com"_q
			|| host.endsWith(u".sndcdn.com"_q)
			|| host.endsWith(u".soundcloud.cloud"_q));
}

[[nodiscard]] bool ValidTrackId(const QString &id) {
	const auto number = id.toULongLong();
	return number != 0 && QString::number(number) == id;
}

[[nodiscard]] QString ResourceId(
		const QJsonObject &object,
		const QString &field) {
	auto raw = object.value(field).toString();
	if (raw.isEmpty() && object.value(field).isDouble()) {
		raw = QString::number(object.value(field).toVariant().toLongLong());
	}
	if (raw.isEmpty() && field == u"id"_q) {
		raw = object.value("urn").toString();
	}
	const auto match = QRegularExpression(u"(?:^|:)([0-9]+)$"_q).match(raw);
	if (!match.hasMatch()) {
		return QString();
	}
	const auto number = match.captured(1).toULongLong();
	return number ? QString::number(number) : QString();
}

[[nodiscard]] QString ClientIdFile() {
	return SoundCloudCacheDir() + u"/client_id.txt"_q;
}

QString &ClientId() {
	static auto id = [] {
		auto file = QFile(ClientIdFile());
		return file.open(QIODevice::ReadOnly)
			? QString::fromUtf8(file.readAll()).trimmed()
			: QString();
	}();
	return id;
}

QNetworkReply *Get(const QUrl &url, qint64 limit = 0, int timeoutMs = 20000) {
	auto request = QNetworkRequest(url);
	request.setRawHeader("User-Agent", kUserAgent);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(timeoutMs);
	const auto reply = Net().get(request);
	if (limit > 0) {
		reply->setReadBufferSize(limit + 1);
		QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
			[=](qint64 received, qint64 total) {
				if (received > limit || total > limit) {
					reply->abort();
				}
			});
	}
	return reply;
}

// client_id lives in the public JS bundles of the site. The scripts are found
// through the front page HTML, walked from the newest backwards, and the first
// one that carries a 32-char client_id wins. It is cached on disk until the
// API starts answering 401/403, then refreshed once and retried.
void FetchClientId(Fn<void(QString)> done) {
	const auto home = Get(QUrl(QString::fromLatin1(kHome)));
	QObject::connect(home, &QNetworkReply::finished, [=] {
		const auto html = QString::fromUtf8(home->readAll());
		home->deleteLater();
		auto scripts = QStringList();
		auto match = QRegularExpression(
			uR"(https://a-v2\.sndcdn\.com/assets/[^"']+\.js)"_q
		).globalMatch(html);
		while (match.hasNext()) {
			scripts.push_back(match.next().captured());
		}
		if (scripts.isEmpty()) {
			done({});
			return;
		}
		std::reverse(scripts.begin(), scripts.end());
		const auto next = std::make_shared<Fn<void(int)>>();
		*next = [=](int index) {
			if (index >= scripts.size()) {
				done({});
				return;
			}
			if (!SafeMediaUrl(QUrl(scripts[index]))) {
				(*next)(index + 1);
				return;
			}
			const auto reply = Get(QUrl(scripts[index]));
			QObject::connect(reply, &QNetworkReply::finished, [=] {
				const auto text = QString::fromUtf8(reply->readAll());
				reply->deleteLater();
				const auto found = QRegularExpression(
					uR"(client_id["']?\s*[:=]\s*["']([A-Za-z0-9]{32})["'])"_q
				).match(text);
				if (found.hasMatch()) {
					done(found.captured(1));
				} else {
					(*next)(index + 1);
				}
			});
		};
		(*next)(0);
	});
}

void EnsureClientId(Fn<void(bool)> done, bool force = false) {
	if (!force && !ClientId().isEmpty()) {
		done(true);
		return;
	}
	FetchClientId([=](QString id) {
		if (id.isEmpty()) {
			done(false);
			return;
		}
		ClientId() = id;
		QDir().mkpath(SoundCloudCacheDir());
		auto file = QFile(ClientIdFile());
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(id.toUtf8());
		}
		done(true);
	});
}

[[nodiscard]] QString ApiErrorDetail(const QByteArray &data) {
	const auto root = QJsonDocument::fromJson(data).object();
	auto message = root.value("message").toString().trimmed();
	if (message.isEmpty()) {
		message = root.value("error_message").toString().trimmed();
	}
	if (message.isEmpty()) {
		const auto errors = root.value("errors").toArray();
		if (!errors.isEmpty()) {
			message = errors.first().toObject()
				.value("error_message").toString().trimmed();
		}
	}
	return message.left(200);
}

void ApiGet(
		const QString &base,
		QUrlQuery query,
		Fn<void(QByteArray, QString)> done,
		bool retried = false) {
	const auto send = [=](bool ok) {
		if (!ok) {
			done({}, tr::hoursgram_MusicScBadClientId(tr::now));
			return;
		}
		auto url = QUrl(base);
		auto copy = QUrlQuery(url);
		for (const auto &item : query.queryItems()) {
			copy.removeAllQueryItems(item.first);
			copy.addQueryItem(item.first, item.second);
		}
		if (!ClientId().isEmpty()) {
			copy.removeAllQueryItems(u"client_id"_q);
			copy.addQueryItem(u"client_id"_q, ClientId());
		}
		url.setQuery(copy);
		const auto reply = Get(url);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			const auto status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const auto data = reply->readAll();
			const auto error = reply->error();
			reply->deleteLater();
			if ((status == 401 || status == 403) && !retried) {
				EnsureClientId([=](bool refreshed) {
					if (!refreshed) {
						done({}, tr::hoursgram_MusicScStaleClientId(tr::now));
						return;
					}
					ApiGet(base, query, done, true);
				}, true);
				return;
			}
			if (status == 401 || status == 403) {
				auto message = tr::hoursgram_MusicScDenied(tr::now);
				const auto detail = ApiErrorDetail(data);
				if (!detail.isEmpty()) {
					message += u" "_q + detail;
				}
				done({}, std::move(message));
				return;
			}
			if (status == 429) {
				done({}, tr::hoursgram_MusicScRateLimited(tr::now));
				return;
			}
			if (error != QNetworkReply::NoError) {
				done({}, tr::hoursgram_MusicScNetwork(
					tr::now,
					lt_error,
					reply->errorString()));
				return;
			}
			done(data, QString());
		});
	};
	EnsureClientId(send, false);
}

[[nodiscard]] ScTrack ParseTrack(const QJsonObject &object) {
	auto result = ScTrack();
	result.id = ResourceId(object, u"id"_q);
	result.title = object.value("title").toString();
	const auto user = object.value("user").toObject();
	result.artist = object.value("publisher_metadata").toObject()
		.value("artist").toString().trimmed();
	if (result.artist.isEmpty()) {
		result.artist = user.value("username").toString();
	}
	result.artworkUrl = object.value("artwork_url").toString();
	if (result.artworkUrl.isEmpty()) {
		result.artworkUrl = user.value("avatar_url").toString();
	}
	result.permalink = object.value("permalink_url").toString();
	result.durationMs = object.value("duration").toInt();
	result.authorization = object.value("track_authorization").toString();

	auto progressive = QString();
	auto hlsMp3 = QString();
	auto hlsAac = QString();
	for (const auto &value : object.value("media").toObject()
			.value("transcodings").toArray()) {
		const auto item = value.toObject();
		const auto format = item.value("format").toObject();
		const auto protocol = format.value("protocol").toString();
		const auto mime = format.value("mime_type").toString();
		const auto url = item.value("url").toString();
		if (item.value("snipped").toBool()) {
			continue;
		}
		if (protocol == u"progressive"_q && mime.contains(u"mpeg"_q)) {
			progressive = url;
		} else if (protocol == u"hls"_q && mime.contains(u"mpeg"_q)
			&& !mime.contains(u"mpegurl"_q)) {
			hlsMp3 = url;
		} else if (protocol == u"hls"_q && mime.contains(u"mp4"_q)) {
			hlsAac = url;
		}
	}
	if (!progressive.isEmpty()) {
		result.transcodingUrl = progressive;
		result.extension = u"mp3"_q;
	} else if (!hlsAac.isEmpty()) {
		result.transcodingUrl = hlsAac;
		result.extension = u"m4a"_q;
		result.hls = true;
	} else if (!hlsMp3.isEmpty()) {
		result.transcodingUrl = hlsMp3;
		result.extension = u"mp3"_q;
		result.hls = true;
	}
	return result;
}

[[nodiscard]] QString CachePath(const ScTrack &track) {
	if (!ValidTrackId(track.id)) {
		return QString();
	}
	return SoundCloudCacheDir() + '/' + track.id + '.' + track.extension;
}

void FinishFile(
		const QString &path,
		const QByteArray &bytes,
		Fn<void(const QString &, const QString &)> done) {
	QDir().mkpath(SoundCloudCacheDir());
	auto file = QFile(path + u".part"_q);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		|| file.write(bytes) != bytes.size()) {
		done({}, tr::hoursgram_MusicScWriteFailed(tr::now));
		return;
	}
	file.close();
	QFile::remove(path);
	file.rename(path);
	done(path, QString());
}

// An HLS stream is downloaded as its init segment plus the media segments in
// order, concatenated into a single file. Segment URLs are resolved against
// the playlist URL and each one is re-checked, because a playlist could point
// anywhere.
void DownloadHls(
		const QString &playlistUrl,
		const QString &path,
		Fn<void(const QString &, const QString &)> done) {
	const auto reply = Get(QUrl(playlistUrl), kMaxAudioBytes);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto text = QString::fromUtf8(reply->readAll());
		const auto base = reply->url();
		const auto failed = (reply->error() != QNetworkReply::NoError);
		reply->deleteLater();
		if (failed) {
			done({}, tr::hoursgram_MusicScDownloadFailed(tr::now));
			return;
		}
		auto parts = QStringList();
		for (const auto &raw : text.split('\n')) {
			const auto line = raw.trimmed();
			if (line.startsWith(u"#EXT-X-MAP:"_q)) {
				const auto found = QRegularExpression(
					u"URI=\"([^\"]+)\""_q).match(line);
				if (found.hasMatch()) {
					parts.push_back(base.resolved(
						QUrl(found.captured(1))).toString());
				}
			} else if (!line.isEmpty() && !line.startsWith('#')) {
				parts.push_back(base.resolved(QUrl(line)).toString());
			}
		}
		if (parts.isEmpty() || parts.size() > kMaxHlsSegments) {
			done({}, tr::hoursgram_MusicScDownloadFailed(tr::now));
			return;
		}
		for (const auto &part : parts) {
			if (!SafeMediaUrl(QUrl(part))) {
				done({}, tr::hoursgram_MusicScBadLink(tr::now));
				return;
			}
		}
		const auto buffer = std::make_shared<QByteArray>();
		const auto next = std::make_shared<Fn<void(int)>>();
		*next = [=](int index) {
			if (index >= parts.size()) {
				FinishFile(path, *buffer, done);
				return;
			}
			const auto segment = Get(QUrl(parts[index]), kMaxAudioBytes);
			QObject::connect(segment, &QNetworkReply::finished, [=] {
				const auto ok = (segment->error() == QNetworkReply::NoError);
				if (ok) {
					buffer->append(segment->readAll());
				}
				segment->deleteLater();
				if (!ok || buffer->size() > kMaxAudioBytes) {
					done({}, tr::hoursgram_MusicScDownloadFailed(tr::now));
					return;
				}
				(*next)(index + 1);
			});
		};
		(*next)(0);
	});
}

using ImageCallbacks = std::map<QString, std::vector<Fn<void(QImage)>>>;

[[nodiscard]] std::deque<QString> &QueuedImages() {
	static auto result = std::deque<QString>();
	return result;
}

int &ActiveImageRequests() {
	static auto result = 0;
	return result;
}

[[nodiscard]] std::map<QString, QImage> &ImageCache() {
	static auto result = std::map<QString, QImage>();
	return result;
}

[[nodiscard]] ImageCallbacks &PendingImages() {
	static auto result = ImageCallbacks();
	return result;
}

void StartQueuedImageLoads();

void FinishImageRequest(const QString &url, QImage image) {
	if (!image.isNull()) {
		auto &cache = ImageCache();
		if (cache.size() > 300) {
			cache.clear();
		}
		cache[url] = image;
	}
	ActiveImageRequests() = std::max(ActiveImageRequests() - 1, 0);
	auto callbacks = std::vector<Fn<void(QImage)>>();
	auto &pending = PendingImages();
	if (const auto i = pending.find(url); i != pending.end()) {
		callbacks = std::move(i->second);
		pending.erase(i);
	}
	StartQueuedImageLoads();
	for (const auto &callback : callbacks) {
		callback(image);
	}
}

void LoadImageAttempt(const QString &url, int attempt) {
	const auto reply = Get(QUrl(url), kMaxImageBytes, 5000);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		auto image = QImage::fromData(reply->readAll());
		reply->deleteLater();
		if (image.isNull() && attempt == 0) {
			QTimer::singleShot(400, QCoreApplication::instance(), [=] {
				LoadImageAttempt(url, 1);
			});
			return;
		}
		FinishImageRequest(url, std::move(image));
	});
}

void StartQueuedImageLoads() {
	auto &queue = QueuedImages();
	while (ActiveImageRequests() < kConcurrentImages && !queue.empty()) {
		auto url = std::move(queue.front());
		queue.pop_front();
		if (PendingImages().find(url) == PendingImages().end()) {
			continue;
		}
		++ActiveImageRequests();
		LoadImageAttempt(url, 0);
	}
}

} // namespace

void SoundCloudLoadImage(const QString &url, Fn<void(QImage)> done) {
	const auto &cache = ImageCache();
	if (const auto i = cache.find(url); i != cache.end()) {
		done(i->second);
		return;
	}
	if (url.isEmpty() || !SafeMediaUrl(QUrl(url))) {
		done(QImage());
		return;
	}
	auto &pending = PendingImages();
	if (const auto i = pending.find(url); i != pending.end()) {
		i->second.push_back(std::move(done));
		return;
	}
	pending[url].push_back(std::move(done));
	QueuedImages().push_back(url);
	StartQueuedImageLoads();
}

void SoundCloudSearch(
		const QString &query,
		int offset,
		Fn<void(ScSearchResult)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"q"_q, query);
	q.addQueryItem(u"limit"_q, QString::number(kPageSize));
	q.addQueryItem(u"offset"_q, QString::number(offset));
	ApiGet(
		QString::fromLatin1(kApi) + u"/search/tracks"_q,
		q,
		[=](QByteArray data, QString error) {
			auto result = ScSearchResult();
			if (!error.isEmpty()) {
				result.error = error;
				done(std::move(result));
				return;
			}
			const auto root = QJsonDocument::fromJson(data).object();
			for (const auto &value : root.value("collection").toArray()) {
				auto track = ParseTrack(value.toObject());
				if (!track.transcodingUrl.isEmpty()) {
					result.tracks.push_back(std::move(track));
				}
			}
			if (!root.value("next_href").toString().isEmpty()) {
				result.nextOffset = offset + kPageSize;
			}
			done(std::move(result));
		});
}

void SoundCloudResolve(
		const ScTrack &track,
		Fn<void(ScTrack, const QString &error)> done) {
	if (!ValidTrackId(track.id)) {
		done(track, tr::hoursgram_MusicScBadId(tr::now));
		return;
	}
	ApiGet(
		QString::fromLatin1(kApi) + u"/tracks/"_q + track.id,
		QUrlQuery(),
		[=](QByteArray data, QString error) {
			if (!error.isEmpty()) {
				done(track, error);
				return;
			}
			auto fresh = ParseTrack(QJsonDocument::fromJson(data).object());
			if (fresh.transcodingUrl.isEmpty()) {
				done(track, tr::hoursgram_MusicScUnavailable(tr::now));
				return;
			}
			done(std::move(fresh), QString());
		});
}

void SoundCloudDownload(
		const ScTrack &track,
		Fn<void(const QString &path, const QString &error)> done) {
	const auto path = CachePath(track);
	if (path.isEmpty()) {
		done({}, tr::hoursgram_MusicScBadId(tr::now));
		return;
	}
	if (QFile::exists(path)) {
		done(path, QString());
		return;
	}
	using Waiters = std::vector<Fn<void(const QString &, const QString &)>>;
	static auto loading = std::map<QString, Waiters>();
	if (const auto i = loading.find(path); i != loading.end()) {
		i->second.push_back(std::move(done));
		return;
	}
	loading[path].push_back(std::move(done));
	const auto finish = [=](const QString &result, const QString &error) {
		const auto i = loading.find(path);
		if (i != loading.end()) {
			auto waiters = std::move(i->second);
			loading.erase(i);
			for (const auto &waiter : waiters) {
				waiter(result, error);
			}
		}
	};
	auto q = QUrlQuery();
	if (!track.authorization.isEmpty()) {
		q.addQueryItem(u"track_authorization"_q, track.authorization);
	}
	if (!SafeMediaUrl(QUrl(track.transcodingUrl))) {
		finish({}, tr::hoursgram_MusicScBadLink(tr::now));
		return;
	}
	ApiGet(track.transcodingUrl, q, [=](QByteArray data, QString error) {
		if (!error.isEmpty()) {
			finish({}, error);
			return;
		}
		const auto url = QJsonDocument::fromJson(data).object()
			.value("url").toString();
		if (url.isEmpty() || !SafeMediaUrl(QUrl(url))) {
			finish({}, tr::hoursgram_MusicScBadLink(tr::now));
			return;
		}
		if (track.hls) {
			DownloadHls(url, path, finish);
			return;
		}
		const auto reply = Get(QUrl(url), kMaxAudioBytes);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			const auto bytes = reply->readAll();
			const auto failed = (reply->error() != QNetworkReply::NoError);
			reply->deleteLater();
			if (failed) {
				finish({}, tr::hoursgram_MusicScDownloadFailed(tr::now));
				return;
			}
			FinishFile(path, bytes, finish);
		});
	});
}

QString SoundCloudCacheDir() {
	return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
		+ u"/hoursgram_music"_q;
}

} // namespace HoursGram::Music
