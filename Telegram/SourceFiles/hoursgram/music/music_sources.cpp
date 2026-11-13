/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_sources.h"

#include "apiwrap.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "data/data_audio_msg_id.h"
#include "base/weak_ptr.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "history/history.h"
#include "data/data_media_types.h"
#include "data/data_messages.h"
#include "data/data_peer.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "hoursgram/music/music_common.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace HoursGram::Music {
namespace {

constexpr auto kMaxResponseSize = 4 * 1024 * 1024;
constexpr auto kMaxArtworkSize = 2 * 1024 * 1024;
constexpr auto kChartLimit = 100;
constexpr auto kSearchLimit = 50;
constexpr auto kCatalogPerSource = 25;
constexpr auto kCatalogLimit = 40;
constexpr auto kSavedLimit = 100;
constexpr auto kFreshDays = 45;
constexpr auto kMinFresh = 12;
constexpr auto kMaxArtworkCached = 400;
constexpr auto kFreshWaveLifetime = crl::time(30 * 60 * 1000);
constexpr auto kUserAgent = "HoursGram Desktop "
	"(https://github.com/plshours/HoursGram-Desktop)";

// Never destroyed: a Qt object that is a static of a function would be taken
// down after the application object, which is too late for it.
[[nodiscard]] QNetworkAccessManager &Manager() {
	static auto *result = new QNetworkAccessManager();
	return *result;
}

void Get(const QString &url, int limit, Fn<void(QByteArray)> done) {
	auto request = QNetworkRequest(QUrl(url));
	request.setRawHeader("User-Agent", kUserAgent);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(15000);
	const auto reply = Manager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		auto bytes = (reply->error() == QNetworkReply::NoError)
			? reply->read(limit + 1)
			: QByteArray();
		reply->deleteLater();
		done((bytes.size() > limit) ? QByteArray() : std::move(bytes));
	});
}

[[nodiscard]] QString AppleCountry() {
	return UsesRussian() ? u"ru"_q : u"us"_q;
}

[[nodiscard]] std::vector<ChartTrack> ParseApple(const QByteArray &bytes) {
	auto result = std::vector<ChartTrack>();
	const auto feed = QJsonDocument::fromJson(bytes).object().value(
		u"feed"_q).toObject();
	auto rank = 0;
	for (const auto &value : feed.value(u"results"_q).toArray()) {
		const auto entry = value.toObject();
		auto track = ChartTrack{
			.title = entry.value(u"name"_q).toString().trimmed(),
			.artist = entry.value(u"artistName"_q).toString().trimmed(),
			.artworkUrl = entry.value(u"artworkUrl100"_q).toString(),
			.released = QDate::fromString(
				entry.value(u"releaseDate"_q).toString(),
				Qt::ISODate),
			.rank = ++rank,
		};
		if (track.title.isEmpty() || track.artist.isEmpty()) {
			continue;
		}
		track.artworkUrl.replace(u"/100x100bb."_q, u"/300x300bb."_q);
		result.push_back(std::move(track));
	}
	return result;
}

[[nodiscard]] std::vector<ChartTrack> ParseDeezer(const QByteArray &bytes) {
	auto result = std::vector<ChartTrack>();
	const auto root = QJsonDocument::fromJson(bytes).object();
	auto rank = 0;
	for (const auto &value : root.value(u"data"_q).toArray()) {
		const auto entry = value.toObject();
		auto track = ChartTrack{
			.title = entry.value(u"title"_q).toString().trimmed(),
			.artist = entry.value(u"artist"_q).toObject().value(
				u"name"_q).toString().trimmed(),
			.artworkUrl = entry.value(u"album"_q).toObject().value(
				u"cover_medium"_q).toString(),
			.rank = ++rank,
		};
		if (!track.title.isEmpty() && !track.artist.isEmpty()) {
			result.push_back(std::move(track));
		}
	}
	return result;
}

// The answer of the iTunes Search API, which is the Apple Music catalog.
[[nodiscard]] std::vector<ChartTrack> ParseAppleSearch(
		const QByteArray &bytes) {
	auto result = std::vector<ChartTrack>();
	const auto root = QJsonDocument::fromJson(bytes).object();
	auto rank = 0;
	for (const auto &value : root.value(u"results"_q).toArray()) {
		const auto entry = value.toObject();
		if (entry.value(u"kind"_q).toString() != u"song"_q) {
			continue;
		}
		auto track = ChartTrack{
			.title = entry.value(u"trackName"_q).toString().trimmed(),
			.artist = entry.value(u"artistName"_q).toString().trimmed(),
			.artworkUrl = entry.value(u"artworkUrl100"_q).toString(),
			.released = QDate::fromString(
				entry.value(u"releaseDate"_q).toString().left(10),
				Qt::ISODate),
			.rank = ++rank,
		};
		if (track.title.isEmpty() || track.artist.isEmpty()) {
			continue;
		}
		track.artworkUrl.replace(u"/100x100bb."_q, u"/300x300bb."_q);
		result.push_back(std::move(track));
	}
	return result;
}

// The same song is spelled a little differently by the platforms: case,
// "feat." parts and remaster notes in brackets.
[[nodiscard]] QString CatalogKey(const ChartTrack &track) {
	static const auto brackets = QRegularExpression(
		u"\\s*[\\(\\[][^\\)\\]]*[\\)\\]]"_q);
	auto title = track.title.toLower();
	title.remove(brackets);
	auto artist = track.artist.toLower();
	for (const auto separator : { u","_q, u"&"_q, u" feat"_q, u" ft."_q }) {
		const auto index = artist.indexOf(separator);
		if (index > 0) {
			artist = artist.left(index);
		}
	}
	return title.simplified() + QChar('\n') + artist.simplified();
}

// Takes the platforms by turns, so the best matches of both come first.
[[nodiscard]] std::vector<ChartTrack> MergeCatalogs(
		std::vector<ChartTrack> apple,
		std::vector<ChartTrack> deezer) {
	auto result = std::vector<ChartTrack>();
	result.reserve(apple.size() + deezer.size());
	auto keys = base::flat_set<QString>();
	const auto push = [&](ChartTrack &track) {
		if (keys.insert(CatalogKey(track)).second) {
			track.rank = int(result.size()) + 1;
			result.push_back(std::move(track));
		}
	};
	for (auto i = size_t(0); i < std::max(apple.size(), deezer.size()); ++i) {
		if (i < apple.size()) {
			push(apple[i]);
		}
		if (i < deezer.size()) {
			push(deezer[i]);
		}
	}
	if (int(result.size()) > kCatalogLimit) {
		result.resize(kCatalogLimit);
	}
	return result;
}

[[nodiscard]] FreshWave Compose(std::vector<ChartTrack> chart) {
	auto result = FreshWave();
	const auto border = QDate::currentDate().addDays(-kFreshDays);
	for (const auto &track : chart) {
		if (track.released.isValid() && track.released >= border) {
			result.fresh.push_back(track);
		}
	}
	ranges::stable_sort(result.fresh, [](
			const ChartTrack &a,
			const ChartTrack &b) {
		return a.released > b.released;
	});
	if (int(result.fresh.size()) < kMinFresh) {
		for (const auto &track : chart) {
			if (int(result.fresh.size()) >= kMinFresh) {
				break;
			}
			const auto already = ranges::any_of(result.fresh, [&](
					const ChartTrack &other) {
				return (other.rank == track.rank);
			});
			if (!already) {
				result.fresh.push_back(track);
			}
		}
	}
	result.chart = std::move(chart);
	result.failed = result.chart.empty();
	return result;
}

struct FreshWaveCache {
	FreshWave value;
	crl::time loaded = 0;
	bool loading = false;
	std::vector<Fn<void(FreshWave)>> waiting;
};

[[nodiscard]] FreshWaveCache &FreshCache() {
	static auto result = FreshWaveCache();
	return result;
}

void FinishFreshWave(FreshWave value) {
	auto &cache = FreshCache();
	cache.loading = false;
	if (!value.failed) {
		cache.value = value;
		cache.loaded = crl::now();
	}
	for (const auto &callback : base::take(cache.waiting)) {
		callback(value);
	}
}

struct ArtworkEntry {
	QImage image;
	bool loaded = false;
	std::vector<Fn<void(QImage)>> waiting;
};

[[nodiscard]] base::flat_map<QString, ArtworkEntry> &Artworks() {
	static auto result = base::flat_map<QString, ArtworkEntry>();
	return result;
}

[[nodiscard]] std::vector<AudioMsgId> CollectAudio(
		not_null<Main::Session*> session,
		const std::vector<FullMsgId> &ids) {
	auto result = std::vector<AudioMsgId>();
	auto seen = base::flat_set<DocumentId>();
	result.reserve(ids.size());
	for (const auto &id : ids) {
		const auto item = session->data().message(id);
		const auto media = item ? item->media() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (!document
			|| !document->isAudioFile()
			|| media->ttlSeconds()
			|| !seen.emplace(document->id).second) {
			continue;
		}
		result.push_back(AudioMsgId(document, id));
	}
	return result;
}

// Telegram search is by words, so a good candidate has the words of the
// title in its own title and at least a part of the artist name among its
// performers. Covers and remixes still score lower than the original.
[[nodiscard]] int MatchScore(
		const ChartTrack &track,
		not_null<DocumentData*> document) {
	const auto wantedTitle = NormalizeForMatch(track.title);
	const auto wantedArtist = NormalizeForMatch(track.artist);
	const auto title = NormalizeForMatch(TrackTitle(document));
	const auto song = document->song();
	const auto performer = song
		? NormalizeForMatch(song->performer)
		: QString();
	if (wantedTitle.isEmpty() || title.isEmpty()) {
		return 0;
	}
	auto score = 0;
	if (title == wantedTitle) {
		score += 4;
	} else if (title.contains(wantedTitle) || wantedTitle.contains(title)) {
		score += 2;
	} else {
		return 0;
	}
	const auto firstArtist = NormalizeForMatch(
		track.artist.split(QRegularExpression(u"[,&]|\\sx\\s|feat"_q)).value(0));
	if (!performer.isEmpty()) {
		if (performer == wantedArtist) {
			score += 4;
		} else if (!firstArtist.isEmpty() && performer.contains(firstArtist)) {
			score += 3;
		} else if (wantedArtist.contains(performer)) {
			score += 2;
		}
	}
	const auto remix = title.contains(u"remix"_q)
		|| title.contains(u"speedup"_q)
		|| title.contains(u"slowed"_q)
		|| title.contains(u"nightcore"_q)
		|| title.contains(u"cover"_q);
	if (remix && !wantedTitle.contains(u"remix"_q)) {
		score -= 2;
	}
	if (document->duration() < 60 * 1000) {
		score -= 3;
	}
	return score;
}

// A file posted by a verified channel or bot is the full release far more
// often than a re-upload in a random chat, so among equal matches it wins.
[[nodiscard]] int SourceScore(
		not_null<Main::Session*> session,
		const AudioMsgId &audio) {
	const auto item = session->data().message(audio.contextId());
	const auto peer = item ? item->history()->peer.get() : nullptr;
	if (!peer) {
		return 0;
	}
	auto result = 0;
	if (peer->isVerified()) {
		result += 3;
	}
	if (peer->isBroadcast()) {
		result += 1;
	}
	return result;
}

[[nodiscard]] QString CleanTitle(const QString &title) {
	static const auto brackets = QRegularExpression(
		u"\\s*[\\(\\[][^\\)\\]]*[\\)\\]]"_q);
	auto result = title;
	result.replace(brackets, QString());
	return result.trimmed();
}

[[nodiscard]] QString FirstArtist(const QString &artist) {
	static const auto separators = QRegularExpression(
		u"[,&]|\\sx\\s|\\sfeat\\.?\\s|\\sft\\.?\\s"_q,
		QRegularExpression::CaseInsensitiveOption);
	return artist.split(separators).value(0).trimmed();
}

struct ResolveAttempt {
	QString query;
	int threshold = 0;
};

[[nodiscard]] std::vector<ResolveAttempt> ResolveAttempts(
		const ChartTrack &track) {
	const auto title = CleanTitle(track.title);
	const auto artist = FirstArtist(track.artist);
	auto result = std::vector<ResolveAttempt>();
	const auto push = [&](QString query, int threshold) {
		query = query.simplified();
		if (query.isEmpty()) {
			return;
		}
		for (const auto &existing : result) {
			if (existing.query.compare(query, Qt::CaseInsensitive) == 0) {
				return;
			}
		}
		result.push_back({ std::move(query), threshold });
	};
	push(track.artist + ' ' + track.title, 0);
	push(artist + ' ' + title, 0);
	push(title + ' ' + artist, 0);
	push(title, 4);
	return result;
}

void ResolveStep(
		not_null<Main::Session*> session,
		ChartTrack track,
		std::shared_ptr<std::vector<ResolveAttempt>> attempts,
		int index,
		Fn<void(AudioMsgId)> done) {
	if (index >= int(attempts->size())) {
		done(AudioMsgId());
		return;
	}
	const auto attempt = (*attempts)[index];
	const auto weak = base::make_weak(session);
	SearchMusic(session, attempt.query, [=](std::vector<AudioMsgId> found) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		auto best = AudioMsgId();
		auto bestScore = attempt.threshold;
		for (const auto &audio : found) {
			const auto match = MatchScore(track, audio.audio());
			if (match <= 0) {
				continue;
			}
			const auto score = match + SourceScore(strong, audio);
			if (score > bestScore) {
				best = audio;
				bestScore = score;
			}
		}
		if (best) {
			done(best);
		} else {
			ResolveStep(strong, track, attempts, index + 1, done);
		}
	});
}

} // namespace

void LoadFreshWave(Fn<void(FreshWave)> done) {
	auto &cache = FreshCache();
	if (cache.loaded && (crl::now() - cache.loaded < kFreshWaveLifetime)) {
		done(cache.value);
		return;
	}
	cache.waiting.push_back(std::move(done));
	if (cache.loading) {
		return;
	}
	cache.loading = true;
	const auto apple = u"https://rss.marketingtools.apple.com/api/v2/"_q
		+ AppleCountry()
		+ u"/music/most-played/%1/songs.json"_q.arg(kChartLimit);
	Get(apple, kMaxResponseSize, [=](QByteArray bytes) {
		auto chart = ParseApple(bytes);
		if (!chart.empty()) {
			FinishFreshWave(Compose(std::move(chart)));
			return;
		}
		const auto deezer = u"https://api.deezer.com/chart/0/tracks?limit=%1"_q
			.arg(kChartLimit);
		Get(deezer, kMaxResponseSize, [=](QByteArray bytes) {
			FinishFreshWave(Compose(ParseDeezer(bytes)));
		});
	});
}

void SearchMusic(
		not_null<Main::Session*> session,
		const QString &query,
		Fn<void(std::vector<AudioMsgId>)> done) {
	const auto trimmed = query.trimmed();
	if (trimmed.isEmpty()) {
		done({});
		return;
	}
	using Flag = MTPmessages_SearchGlobal::Flag;
	session->api().request(MTPmessages_SearchGlobal(
		MTP_flags(Flag::f_folder_id),
		MTP_int(0),
		MTPInputChannel(),
		MTP_string(trimmed),
		MTP_inputMessagesFilterMusic(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_inputPeerEmpty(),
		MTP_int(0),
		MTP_int(kSearchLimit)
	)).done([=](const MTPmessages_Messages &result) {
		const auto parsed = Api::ParseGlobalMediaResult(session, result);
		auto ids = std::vector<FullMsgId>();
		ids.reserve(parsed.messageIds.size());
		for (const auto &position : parsed.messageIds) {
			ids.push_back(position.fullId);
		}
		done(CollectAudio(session, ids));
	}).fail([=](const MTP::Error &) {
		done({});
	}).send();
}

void SearchCatalog(
		const QString &query,
		Fn<void(std::vector<ChartTrack>)> done) {
	const auto trimmed = query.trimmed();
	if (trimmed.isEmpty()) {
		done({});
		return;
	}
	struct State {
		std::optional<std::vector<ChartTrack>> apple;
		std::optional<std::vector<ChartTrack>> deezer;
	};
	const auto state = std::make_shared<State>();
	const auto check = [=] {
		if (state->apple && state->deezer) {
			done(MergeCatalogs(
				std::move(*state->apple),
				std::move(*state->deezer)));
		}
	};
	const auto encoded = QString::fromLatin1(
		QUrl::toPercentEncoding(trimmed));
	const auto apple = u"https://itunes.apple.com/search"_q
		+ u"?media=music&entity=song&limit=%1&country=%2&term=%3"_q
			.arg(kCatalogPerSource)
			.arg(AppleCountry(), encoded);
	Get(apple, kMaxResponseSize, [=](QByteArray bytes) {
		state->apple = ParseAppleSearch(bytes);
		check();
	});
	const auto deezer = u"https://api.deezer.com/search/track"_q
		+ u"?limit=%1&q=%2"_q.arg(kCatalogPerSource).arg(encoded);
	Get(deezer, kMaxResponseSize, [=](QByteArray bytes) {
		state->deezer = ParseDeezer(bytes);
		check();
	});
}

void LoadSavedMusic(
		not_null<Main::Session*> session,
		Fn<void(std::vector<AudioMsgId>)> done) {
	const auto peer = not_null<PeerData*>(session->user());
	session->api().request(MTPmessages_Search(
		MTP_flags(MTPmessages_Search::Flag(0)),
		peer->input(),
		MTP_string(QString()),
		MTP_inputPeerEmpty(),
		MTPInputPeer(),
		MTPVector<MTPReaction>(),
		MTP_int(0),
		MTP_inputMessagesFilterMusic(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(kSavedLimit),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		const auto parsed = Api::ParseSearchResult(
			peer,
			Storage::SharedMediaType::MusicFile,
			MsgId(0),
			Data::LoadDirection::Before,
			result);
		auto ids = std::vector<FullMsgId>();
		ids.reserve(parsed.messageIds.size());
		for (const auto &msgId : parsed.messageIds) {
			ids.push_back(FullMsgId(peer->id, msgId));
		}
		done(CollectAudio(session, ids));
	}).fail([=](const MTP::Error &) {
		done({});
	}).send();
}

void ResolveChartTrack(
		not_null<Main::Session*> session,
		const ChartTrack &track,
		Fn<void(AudioMsgId)> done) {
	ResolveStep(
		session,
		track,
		std::make_shared<std::vector<ResolveAttempt>>(ResolveAttempts(track)),
		0,
		std::move(done));
}

void Prefetch(const AudioMsgId &audio) {
	const auto document = audio.audio();
	if (!document || document->loading()) {
		return;
	}
	document->save(Data::FileOriginMessage(audio.contextId()), QString());
}

void LoadArtwork(const QString &url, Fn<void(QImage)> done) {
	if (url.isEmpty()) {
		done(QImage());
		return;
	}
	auto &all = Artworks();
	if (const auto i = all.find(url); i != end(all)) {
		if (i->second.loaded) {
			done(i->second.image);
		} else {
			i->second.waiting.push_back(std::move(done));
		}
		return;
	}
	if (int(all.size()) >= kMaxArtworkCached) {
		for (auto i = begin(all); i != end(all); ++i) {
			if (i->second.loaded) {
				all.erase(i);
				break;
			}
		}
	}
	auto &entry = all[url];
	entry.waiting.push_back(std::move(done));
	Get(url, kMaxArtworkSize, [=](QByteArray bytes) {
		auto image = QImage();
		if (!bytes.isEmpty()) {
			image.loadFromData(bytes);
		}
		auto &all = Artworks();
		const auto i = all.find(url);
		if (i == end(all)) {
			return;
		}
		i->second.loaded = true;
		i->second.image = image;
		for (const auto &callback : base::take(i->second.waiting)) {
			callback(image);
		}
	});
}

} // namespace HoursGram::Music
