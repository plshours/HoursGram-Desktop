// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 80e3dd12
//
// HoursGram: taken from Telegram/SourceFiles/mokagram/music/mokagram_lyrics.cpp
// of mokaGram and changed: moved to the HoursGram::Music::Sources namespace,
// the artwork lookup, the Genius source and the in-memory cache left out,
// the requests name HoursGram in their user agent.
#include "hoursgram/music/music_lyrics_sources.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include "logs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace HoursGram::Music::Sources {
namespace {

QNetworkAccessManager &Net() {
	static auto manager = new QNetworkAccessManager();
	return *manager;
}

constexpr auto kMaxTextBytes = 4 * 1024 * 1024;     // json/html-ответы источников

// Разрешаем только https на известных хостах: ответы API не должны уводить загрузку
// на file:// или чужой сервер. mzstatic.com и dzcdn.net — каталоги обложек iTunes и Deezer.
[[nodiscard]] bool SafeMediaUrl(const QUrl &url) {
	const auto host = url.host();
	return url.isValid()
		&& url.scheme() == u"https"_q
		&& url.userInfo().isEmpty()
		&& url.port(-1) == -1
		&& (host == u"lrclib.net"_q
			|| host == u"genius.com"_q);
}

// Слишком большие ответы обрываем: readAll() не должен собирать гигабайты.
void LimitReply(QNetworkReply *reply, qint64 limit) {
	reply->setReadBufferSize(limit + 1);
	QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
		[=](qint64 received, qint64 total) {
			if (received > limit || total > limit) {
				reply->abort();
			}
		});
}

constexpr auto kTooManyRequests = 429;
constexpr auto kRetries = 2;
constexpr auto kRetryDelay = 1500;

[[nodiscard]] Lyrics ParseLrc(const QString &text) {
	auto result = Lyrics();
	static const auto stamp = QRegularExpression(
		uR"(\[(\d+):(\d+)(?:[.:](\d+))?\])"_q);
	for (const auto &raw : text.split('\n')) {
		auto times = std::vector<qint64>();
		auto last = 0;
		auto it = stamp.globalMatch(raw);
		while (it.hasNext()) {
			const auto m = it.next();
			auto fraction = m.captured(3);
			const auto ms = fraction.isEmpty()
				? 0
				: (fraction.leftJustified(3, '0').left(3).toInt());
			times.push_back(
				m.captured(1).toLongLong() * 60000
				+ m.captured(2).toLongLong() * 1000
				+ ms);
			last = m.capturedEnd();
		}
		if (times.empty()) {
			continue;
		}
		const auto line = raw.mid(last).trimmed();
		for (const auto time : times) {
			result.lines.push_back({ time, line });
		}
	}
	std::sort(result.lines.begin(), result.lines.end(), [](const auto &a, const auto &b) {
		return a.timeMs < b.timeMs;
	});
	result.synced = !result.lines.empty();
	result.found = result.synced;
	return result;
}

// Версии speed up и slowed идут быстрее или медленнее оригинала: метки времени
// растягиваем во столько же раз, во сколько отличается длина трека.
// Растягиваем метки, только если версия реально другой скорости.
// Разница до 12 секунд это тишина или интро в начале/конце, а не скорость:
// растяжка тогда копит отставание к концу песни.
[[nodiscard]] double SpeedRatio(double durationMs, double sourceMs) {
	if (durationMs <= 0. || sourceMs <= 0.
		|| std::abs(sourceMs - durationMs) <= 12000.) {
		return 1.;
	}
	return durationMs / sourceMs;
}

void ScaleTimes(Lyrics &lyrics, double ratio) {
	if (std::abs(ratio - 1.) < 0.01) {
		return;
	}
	for (auto &line : lyrics.lines) {
		line.timeMs = qint64(line.timeMs * ratio);
		for (auto &word : line.words) {
			word.timeMs = qint64(word.timeMs * ratio);
			if (word.durationMs > 0) {
				word.durationMs = qint64(word.durationMs * ratio);
			}
		}
	}
}

[[nodiscard]] Lyrics ParsePlain(const QString &text) {
	auto result = Lyrics();
	for (const auto &raw : text.split('\n')) {
		result.lines.push_back({ 0, raw.trimmed() });
	}
	result.found = !result.lines.empty();
	return result;
}

// Название без любых скобок: пометки вроде "(slowed + reverb)" ищут хуже всего.
[[nodiscard]] QString WithoutBrackets(const QString &text) {
	static const auto brackets = QRegularExpression(uR"(\s*[\(\[][^\)\]]*[\)\]])"_q);
	auto result = text;
	result.remove(brackets);
	result = result.trimmed();
	return result.isEmpty() ? text : result;
}

[[nodiscard]] Lyrics Pick(
		const QJsonArray &list,
		qint64 durationMs,
		const QString &matchingTitle = {}) {
	auto best = QJsonObject();
	auto bestScore = -1e9;
	auto bestSynced = false;
	auto bestRatio = 1.;
	for (const auto &value : list) {
		const auto item = value.toObject();
		const auto synced = item.value("syncedLyrics").toString();
		const auto plain = item.value("plainLyrics").toString();
		if (synced.isEmpty() && plain.isEmpty()) {
			continue;
		}
		const auto source = item.value("duration").toDouble() * 1000.;
		const auto ratio = SpeedRatio(durationMs, source);
		const auto diff = std::abs(source - durationMs);
		if (!matchingTitle.isEmpty()) {
			const auto title = WithoutBrackets(CleanTitle(
				item.value("trackName").toString(),
				item.value("artistName").toString()));
			if (title.compare(matchingTitle, Qt::CaseInsensitive) != 0
				|| durationMs <= 0
				|| source <= 0.
				|| diff > 1000.) {
				continue;
			}
		}
		auto useSynced = false;
		auto score = 10. - std::min(diff / 1000., 9.);
		if (!synced.isEmpty() && (durationMs <= 0 || diff <= 12000.)) {
			useSynced = true;
			score = 100. - diff / 1000.;
		} else if (!synced.isEmpty() && ratio > 0.5 && ratio < 2.) {
			// Другая скорость: метки подгоним по отношению длин.
			useSynced = true;
			score = 60. - std::abs(std::log(ratio)) * 20.;
		}
		if (score > bestScore) {
			bestScore = score;
			best = item;
			bestSynced = useSynced;
			bestRatio = ratio;
		}
	}
	if (bestScore < -1e8) {
		return {};
	}
	// Названия в лог не пишем: запросы могут быть личными.
	LOG(("[lyrics] lrclib picked a variant (%1 s)")
		.arg(best.value("duration").toDouble()));
	if (bestSynced) {
		auto result = ParseLrc(best.value("syncedLyrics").toString());
		if (result.found) {
			ScaleTimes(result, bestRatio);
			return result;
		}
	}
	const auto plain = best.value("plainLyrics").toString();
	if (!plain.isEmpty()) {
		return ParsePlain(plain);
	}
	return ParsePlain(best.value("syncedLyrics").toString().remove(
		QRegularExpression(uR"(\[[^\]]*\])"_q)));
}

void Request(
		QUrlQuery query,
		qint64 durationMs,
		Fn<void(Lyrics)> done,
		QString matchingTitle = {},
		int attempt = 0) {
	// '+' и скобки кодируем сами: QUrlQuery оставляет '+' как есть, и сервис читает его пробелом.
	auto parts = QStringList();
	for (const auto &[key, value] : query.queryItems()) {
		parts.push_back(key + u"="_q + QString::fromLatin1(QUrl::toPercentEncoding(value)));
	}
	auto url = QUrl(u"https://lrclib.net/api/search?"_q + parts.join('&'));
	if (!SafeMediaUrl(url)) {
		done(Lyrics{ .unavailable = true });
		return;
	}
	auto request = QNetworkRequest(url);
	request.setRawHeader("User-Agent", "HoursGram Desktop (https://github.com/plshours/HoursGram-Desktop), lyrics lookup from mokaGram");
	request.setTransferTimeout(15000);
	const auto reply = Net().get(request);
	LimitReply(reply, kMaxTextBytes);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto result = Lyrics();
		// В лог идёт только хост: полный URL содержит название трека.
		LOG(("[lyrics] %1: error %2").arg(url.host()).arg(reply->errorString()));
		// lrclib режет частые запросы: ждём и пробуем ещё, а не «недоступен».
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == kTooManyRequests && attempt < kRetries) {
			QTimer::singleShot(kRetryDelay * (attempt + 1), [=] {
				Request(query, durationMs, done, matchingTitle, attempt + 1);
			});
			return;
		}
		if (reply->error() == QNetworkReply::NoError) {
			const auto doc = QJsonDocument::fromJson(reply->readAll());
			if (doc.isArray()) {
				result = Pick(doc.array(), durationMs, matchingTitle);
			} else {
				result.unavailable = true;
			}
		} else {
			result.unavailable = true;
		}
		done(std::move(result));
	});
}

void GetJson(
		const QString &address,
		Fn<void(QJsonDocument)> done,
		QByteArray referer = "https://music.163.com/") {
	auto request = QNetworkRequest(QUrl(address));
	request.setRawHeader("User-Agent", "Mozilla/5.0");
	request.setRawHeader("Referer", referer);
	request.setTransferTimeout(15000);
	const auto reply = Net().get(request);
	LimitReply(reply, kMaxTextBytes);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto doc = (reply->error() == QNetworkReply::NoError)
			? QJsonDocument::fromJson(reply->readAll())
			: QJsonDocument();
		done(doc.object().value("code").toInt(200) == 200
			&& doc.object().value("status").toInt(200) == 200
			? doc
			: QJsonDocument());
	});
}

// KRC, the lyrics of Kugou with a timing for every word: "krc1" and then a
// zlib stream XORed with a fixed key. Lines look like
// [start,duration]<offset,duration,0>word<offset,duration,0>word, with the
// word offsets counted from the start of the line.
[[nodiscard]] QString DecodeKrc(const QByteArray &bytes) {
	if (!bytes.startsWith("krc1")) {
		return QString();
	}
	static const auto key = std::array<uchar, 16>{
		64, 71, 97, 119, 94, 50, 116, 71,
		81, 54, 49, 45, 206, 210, 110, 105,
	};
	auto data = bytes.mid(4);
	for (auto i = 0, count = int(data.size()); i != count; ++i) {
		data[i] = char(uchar(data[i]) ^ key[i % 16]);
	}
	// qUncompress wants the size of the result in front, big endian; a
	// guess is enough, the buffer grows when it is too small.
	const auto guess = quint32(std::min<qint64>(
		qint64(data.size()) * 8,
		kMaxTextBytes));
	auto sized = QByteArray(4, Qt::Uninitialized);
	sized[0] = char((guess >> 24) & 0xFF);
	sized[1] = char((guess >> 16) & 0xFF);
	sized[2] = char((guess >> 8) & 0xFF);
	sized[3] = char(guess & 0xFF);
	sized += data;
	const auto inflated = qUncompress(sized);
	if (inflated.isEmpty() || inflated.size() > kMaxTextBytes) {
		return QString();
	}
	auto result = QString::fromUtf8(inflated);
	if (result.startsWith(QChar(0xFEFF))) {
		result.remove(0, 1);
	}
	return result;
}

[[nodiscard]] Lyrics ParseKrc(const QString &text) {
	auto result = Lyrics();
	static const auto lineExp = QRegularExpression(
		uR"(^\[(\d+),(\d+)\](.*)$)"_q);
	static const auto wordExp = QRegularExpression(
		uR"(<(\d+),(\d+),-?\d+>([^<]*))"_q);
	for (auto raw : text.split('\n')) {
		raw.remove('\r');
		const auto match = lineExp.match(raw);
		if (!match.hasMatch()) {
			continue;
		}
		auto line = LyricLine{ .timeMs = match.captured(1).toLongLong() };
		const auto rest = match.captured(3);
		auto it = wordExp.globalMatch(rest);
		while (it.hasNext()) {
			const auto word = it.next();
			const auto text = word.captured(3);
			if (text.isEmpty()) {
				continue;
			}
			line.words.push_back({
				.timeMs = line.timeMs + word.captured(1).toLongLong(),
				.durationMs = word.captured(2).toLongLong(),
				.text = text,
			});
		}
		if (line.words.empty()) {
			line.text = rest.trimmed();
		} else {
			auto &first = line.words.front().text;
			while (!first.isEmpty() && first.front().isSpace()) {
				first.remove(0, 1);
			}
			auto &last = line.words.back().text;
			while (!last.isEmpty() && last.back().isSpace()) {
				last.chop(1);
			}
			for (const auto &word : line.words) {
				line.text += word.text;
			}
		}
		if (!line.text.isEmpty()) {
			result.words |= !line.words.empty();
			result.lines.push_back(std::move(line));
		}
	}
	std::stable_sort(result.lines.begin(), result.lines.end(), [](
			const LyricLine &a,
			const LyricLine &b) {
		return a.timeMs < b.timeMs;
	});
	result.synced = !result.lines.empty();
	result.found = result.synced;
	return result;
}

struct KugouCandidate {
	QString id;
	QString accesskey;
	double diff = 0.;
	double ratio = 1.;
};

void RequestKugouCandidate(
		std::vector<KugouCandidate> candidates,
		size_t index,
		Fn<void(Lyrics)> done,
		bool unavailable = false) {
	if (index == candidates.size()) {
		done(Lyrics{ .unavailable = unavailable });
		return;
	}
	const auto candidate = candidates[index];
	const auto addressFor = [=](const QString &format) {
		return u"https://lyrics.kugou.com/download?ver=1&client=pc&fmt="_q
			+ format
			+ u"&charset=utf8&id="_q
			+ QString::fromLatin1(QUrl::toPercentEncoding(candidate.id))
			+ u"&accesskey="_q
			+ QString::fromLatin1(QUrl::toPercentEncoding(candidate.accesskey));
	};
	const auto address = addressFor(u"lrc"_q);
	// HoursGram: KRC first, for the timings of the words; plain LRC when
	// there is no KRC or it does not decode.
	const auto lrc = [=] {
		GetJson(address, [=](QJsonDocument doc) {
			const auto encoded = doc.object().value("content").toString().toLatin1();
			const auto decoded = QByteArray::fromBase64Encoding(
				encoded,
				QByteArray::AbortOnBase64DecodingErrors);
			auto result = decoded
				? ParseLrc(QString::fromUtf8(decoded.decoded))
				: Lyrics();
			result.found = result.synced
				&& std::any_of(result.lines.begin(), result.lines.end(), [](const auto &line) {
					return !line.text.isEmpty();
				});
			if (result.found) {
				ScaleTimes(result, candidate.ratio);
				done(std::move(result));
			} else {
				RequestKugouCandidate(candidates, index + 1, done, unavailable || doc.isNull());
			}
		}, "https://www.kugou.com/");
	};
	GetJson(addressFor(u"krc"_q), [=](QJsonDocument doc) {
		const auto encoded = doc.object().value("content").toString().toLatin1();
		const auto decoded = QByteArray::fromBase64Encoding(
			encoded,
			QByteArray::AbortOnBase64DecodingErrors);
		auto result = decoded
			? ParseKrc(DecodeKrc(decoded.decoded))
			: Lyrics();
		if (result.found && result.words) {
			LOG(("[lyrics] kugou krc with word timings"));
			ScaleTimes(result, candidate.ratio);
			done(std::move(result));
		} else {
			lrc();
		}
	}, "https://www.kugou.com/");
}

void RequestKugou(
		const QString &trackName,
		const QString &artistName,
		qint64 durationMs,
		Fn<void(Lyrics)> done) {
	const auto address = u"https://lyrics.kugou.com/search?ver=1&man=yes&client=pc&keyword="_q
		+ QString::fromLatin1(QUrl::toPercentEncoding(artistName + u" "_q + trackName))
		+ u"&duration="_q + QString::number(std::max<qint64>(durationMs, 0));
	GetJson(address, [=](QJsonDocument doc) {
		if (doc.isNull()) {
			done(Lyrics{ .unavailable = true });
			return;
		}
		const auto normalize = [](const QString &text) {
			return text.normalized(QString::NormalizationForm_KC).simplified().toCaseFolded();
		};
		const auto wanted = normalize(trackName);
		const auto artist = normalize(artistName);
		auto candidates = std::vector<KugouCandidate>();
		for (const auto &value : doc.object().value("candidates").toArray()) {
			const auto item = value.toObject();
			const auto singer = normalize(item.value("singer").toString());
			const auto title = normalize(WithoutBrackets(CleanTitle(
				item.value("song").toString(),
				item.value("singer").toString())));
			if (wanted.isEmpty()
				|| title != wanted
				|| (!artist.isEmpty()
					&& (singer.isEmpty()
						|| (!singer.contains(artist) && !artist.contains(singer))))) {
				continue;
			}
			const auto source = item.value("duration").toDouble();
			const auto diff = durationMs > 0 ? std::abs(source - durationMs) : 0.;
			const auto ratio = SpeedRatio(durationMs, source);
			const auto id = item.value("id").toString();
			const auto accesskey = item.value("accesskey").toString();
			if (id.isEmpty()
				|| accesskey.isEmpty()
				|| (durationMs > 0
					&& (source <= 0. || (diff > 12000. && (ratio <= 0.5 || ratio >= 2.))))) {
				continue;
			}
			candidates.push_back({ id, accesskey, diff, ratio });
		}
		std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
			return a.diff < b.diff;
		});
		if (candidates.size() > 5) {
			candidates.resize(5);
		}
		RequestKugouCandidate(std::move(candidates), 0, done);
	}, "https://www.kugou.com/");
}

struct NeteaseCandidate {
	qint64 id = 0;
	double diff = 0.;
	double ratio = 1.;
};

void RequestNeteaseCandidate(
		std::vector<NeteaseCandidate> candidates,
		size_t index,
		Fn<void(Lyrics)> done,
		bool unavailable = false) {
	if (index == candidates.size()) {
		done(Lyrics{ .unavailable = unavailable });
		return;
	}
	const auto candidate = candidates[index];
	GetJson(
		u"https://music.163.com/api/song/lyric?lv=1&kv=1&tv=-1&id="_q
			+ QString::number(candidate.id),
		[=](QJsonDocument lyric) {
			const auto text = lyric.object().value("lrc").toObject().value("lyric").toString();
			auto result = text.isEmpty() ? Lyrics() : ParseLrc(text);
			// Служебные строки с авторами (作词, 作曲...) не показываем.
			static const auto credits = QRegularExpression(
				uR"(^\s*(作词|作曲|编曲|制作|作詞|编曲|词|曲|Lyrics|Music|Composer|Producer|Arranger)\s*[:：])"_q,
				QRegularExpression::CaseInsensitiveOption);
			auto &lines = result.lines;
			lines.erase(
				std::remove_if(lines.begin(), lines.end(), [&](const LyricLine &l) {
					return credits.match(l.text).hasMatch();
				}),
				lines.end());
			result.found = !lines.empty();
			if (result.found) {
				ScaleTimes(result, candidate.ratio);
				done(std::move(result));
			} else {
				RequestNeteaseCandidate(
					candidates,
					index + 1,
					done,
					unavailable || lyric.isNull());
			}
		});
}

// Запасной источник: NetEase отдаёт строки с метками времени для многих треков.
void RequestNetease(
		const QString &query,
		const QString &trackName,
		const QString &artistName,
		qint64 durationMs,
		Fn<void(Lyrics)> done,
		bool strict = false) {
	const auto search = u"https://music.163.com/api/search/get?type=1&limit=8&s="_q
		+ QString::fromLatin1(QUrl::toPercentEncoding(query));
	GetJson(search, [=](QJsonDocument doc) {
		if (doc.isNull()) {
			done(Lyrics{ .unavailable = true });
			return;
		}
		const auto songs = doc.object().value("result").toObject().value("songs").toArray();
		auto candidates = std::vector<NeteaseCandidate>();
		for (const auto &value : songs) {
			const auto song = value.toObject();
			const auto name = song.value("name").toString().toLower();
			const auto wanted = trackName.toLower();
			// Берём только песню с тем же названием, иначе слова будут чужие.
			if (name.isEmpty()
				|| (wanted.size() >= 2
					&& !name.contains(wanted)
					&& !wanted.contains(name))) {
				continue;
			}
			// Тот же исполнитель или точное название и почти та же длительность:
			// «название содержит слово» без этого даёт чужие песни.
			auto artistOk = false;
			const auto wantedArtist = artistName.toLower();
			for (const auto &performer : song.value("artists").toArray()) {
				const auto who = performer.toObject().value("name").toString().toLower();
				if (!wantedArtist.isEmpty()
					&& (who.contains(wantedArtist) || wantedArtist.contains(who))) {
					artistOk = true;
				}
			}
			const auto exact = (WithoutBrackets(name) == WithoutBrackets(wanted));
			const auto source = song.value("duration").toDouble();
			const auto diff = durationMs > 0
				? std::abs(source - durationMs)
				: 0.;
			const auto ratio = SpeedRatio(durationMs, source);
			const auto id = qint64(song.value("id").toDouble());
			if (!id
				|| (!artistOk && (!exact || durationMs <= 0 || diff > 1000.))
				|| (strict && durationMs > 0 && diff > 4000.)
				|| (durationMs > 0
					&& diff > 12000.
					&& (ratio <= 0.5 || ratio >= 2.))) {
				continue;
			}
			candidates.push_back({ id, diff, ratio });
		}
		std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
			return a.diff < b.diff;
		});
		RequestNeteaseCandidate(std::move(candidates), 0, done);
	});
}

// Genius: the public search API needs no key. The lyrics are scraped from
// the song page HTML, where they sit inside data-lyrics-container divs.
void GetHtml(const QString &address, Fn<void(QString)> done) {
	auto url = QUrl(address);
	if (!SafeMediaUrl(url)) {
		done(QString());
		return;
	}
	auto request = QNetworkRequest(url);
	request.setRawHeader("User-Agent",
		"HoursGram Desktop (https://github.com/plshours/HoursGram-Desktop)");
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(15000);
	const auto reply = Net().get(request);
	LimitReply(reply, kMaxTextBytes);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			done(QString());
			return;
		}
		done(QString::fromUtf8(reply->readAll()));
	});
}

[[nodiscard]] Lyrics ParseGeniusHtml(const QString &html) {
	auto result = Lyrics();
	static const auto container = QRegularExpression(
		uR"(data-lyrics-container="true"[^>]*>(.*?)</div>)"_q,
		QRegularExpression::DotMatchesEverythingOption);
	auto text = QString();
	auto it = container.globalMatch(html);
	while (it.hasNext()) {
		auto block = it.next().captured(1);
		block.replace(QRegularExpression(u"<br\\s*/?>"_q), u"\n"_q);
		block.remove(QRegularExpression(u"<[^>]*>"_q));
		block.replace(u"&#x27;"_q, u"'"_q);
		block.replace(u"&amp;"_q, u"&"_q);
		block.replace(u"&lt;"_q, u"<"_q);
		block.replace(u"&gt;"_q, u">"_q);
		block.replace(u"&quot;"_q, u"\""_q);
		if (!text.isEmpty() && !block.isEmpty()) {
			text += '\n';
		}
		text += block;
	}
	if (text.trimmed().isEmpty()) {
		return result;
	}
	for (const auto &raw : text.split('\n')) {
		result.lines.push_back({ 0, raw.trimmed() });
	}
	result.found = !result.lines.empty();
	return result;
}

void RequestGenius(
		const QString &trackName,
		const QString &artistName,
		Fn<void(Lyrics)> done) {
	const auto query = artistName + u" "_q + trackName;
	const auto encoded = QString::fromLatin1(
		QUrl::toPercentEncoding(query));
	const auto url = u"https://genius.com/api/search/multi?q="_q + encoded;
	GetJson(url, [=](QJsonDocument doc) {
		if (doc.isNull()) {
			done({});
			return;
		}
		auto songUrl = QString();
		const auto sections = doc.object().value(
			u"response"_q).toObject().value(
			u"sections"_q).toArray();
		for (const auto &section : sections) {
			const auto obj = section.toObject();
			if (obj.value(u"type"_q).toString() != u"song"_q) {
				continue;
			}
			for (const auto &hit : obj.value(u"hits"_q).toArray()) {
				const auto result = hit.toObject().value(
					u"result"_q).toObject();
				songUrl = result.value(u"url"_q).toString();
				if (!songUrl.isEmpty()) {
					break;
				}
			}
			if (!songUrl.isEmpty()) {
				break;
			}
		}
		if (songUrl.isEmpty() || !songUrl.startsWith(u"https://genius.com/"_q)) {
			done({});
			return;
		}
		GetHtml(songUrl, [=](QString html) {
			done(html.isEmpty() ? Lyrics() : ParseGeniusHtml(html));
		});
	}, "https://genius.com/");
}

// tekst-pesni.com: a site of lyrics with many Russian songs, which the
// other sources often miss. Its WordPress API finds the posts; the lyrics
// are the part of the post under the "Текст песни" heading, up to the next
// heading. A post counts only when its title has most of the words asked.
[[nodiscard]] QString HtmlText(QString html) {
	static const auto blocks = QRegularExpression(
		uR"(<(script|style|iframe)\b[^>]*>.*?</\1>)"_q,
		QRegularExpression::CaseInsensitiveOption
			| QRegularExpression::DotMatchesEverythingOption);
	static const auto br = QRegularExpression(
		uR"(<br\s*/?>)"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto paragraph = QRegularExpression(
		uR"(</(?:p|div)>)"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto tags = QRegularExpression(u"<[^>]*>"_q);
	static const auto entity = QRegularExpression(
		uR"(&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z]+);)"_q);
	static const auto spaces = QRegularExpression(uR"([ \t]+\n)"_q);
	static const auto gaps = QRegularExpression(u"\n{3,}"_q);
	html.remove(blocks);
	html.replace(br, u"\n"_q);
	html.replace(paragraph, u"\n\n"_q);
	html.remove(tags);
	auto result = QString();
	result.reserve(html.size());
	auto from = qsizetype(0);
	auto i = entity.globalMatch(html);
	while (i.hasNext()) {
		const auto match = i.next();
		result += html.mid(from, match.capturedStart() - from);
		from = match.capturedEnd();
		const auto code = match.captured(1);
		if (code.startsWith('#')) {
			auto ok = false;
			const auto value = code.startsWith(u"#x"_q, Qt::CaseInsensitive)
				? code.mid(2).toUInt(&ok, 16)
				: code.mid(1).toUInt(&ok, 10);
			if (ok && value > 0 && value <= 0x10FFFF) {
				const auto unicode = char32_t(value);
				result += QString::fromUcs4(&unicode, 1);
				continue;
			}
		} else {
			static const auto named = std::map<QString, QString>{
				{ u"amp"_q, u"&"_q },
				{ u"lt"_q, u"<"_q },
				{ u"gt"_q, u">"_q },
				{ u"quot"_q, u"\""_q },
				{ u"apos"_q, u"'"_q },
				{ u"nbsp"_q, u" "_q },
				{ u"ndash"_q, QString(QChar(0x2013)) },
				{ u"mdash"_q, QString(QChar(0x2014)) },
				{ u"hellip"_q, QString(QChar(0x2026)) },
				{ u"laquo"_q, QString(QChar(0x00AB)) },
				{ u"raquo"_q, QString(QChar(0x00BB)) },
			};
			const auto found = named.find(code.toLower());
			if (found != named.end()) {
				result += found->second;
				continue;
			}
		}
		result += match.captured(0);
	}
	result += html.mid(from);
	result.replace(spaces, u"\n"_q);
	result.replace(gaps, u"\n\n"_q);
	return result.trimmed();
}

[[nodiscard]] QStringList MatchWords(const QString &text) {
	auto normalized = text.toLower();
	normalized.replace(QChar(0x0451), QChar(0x0435)); // ё -> е
	static const auto other = QRegularExpression(uR"([^\p{L}\p{N}]+)"_q);
	normalized.replace(other, u" "_q);
	return normalized.split(' ', Qt::SkipEmptyParts);
}

// 2 when the title holds the whole query, else the part of its words found.
[[nodiscard]] double Relevance(const QString &title, const QString &query) {
	const auto name = MatchWords(title);
	const auto words = MatchWords(query);
	if (words.isEmpty()) {
		return 0.;
	} else if (name.join(' ').contains(words.join(' '))) {
		return 2.;
	}
	auto found = 0;
	for (const auto &word : words) {
		if (name.contains(word)) {
			++found;
		}
	}
	return double(found) / words.size();
}

constexpr auto kTekstPesniRelevance = 0.6;

void SearchTekstPesni(
		const QString &search,
		const QString &wanted,
		Fn<void(Lyrics)> done) {
	auto query = QUrlQuery();
	query.addQueryItem(u"search"_q, search);
	query.addQueryItem(u"per_page"_q, u"20"_q);
	query.addQueryItem(u"_fields"_q, u"id,title,link,content"_q);
	auto url = QUrl(u"https://tekst-pesni.com/wp-json/wp/v2/posts"_q);
	url.setQuery(query);
	GetJson(url.toString(QUrl::FullyEncoded), [=](QJsonDocument doc) {
		static const auto heading = QRegularExpression(
			uR"(<h[23]\b[^>]*>\s*Текст песни.*?</h[23]>)"_q,
			QRegularExpression::CaseInsensitiveOption
				| QRegularExpression::DotMatchesEverythingOption);
		static const auto next = QRegularExpression(
			uR"(<h[1-6]\b)"_q,
			QRegularExpression::CaseInsensitiveOption);
		auto best = Lyrics();
		auto bestRelevance = 0.;
		for (const auto &value : doc.array()) {
			const auto post = value.toObject();
			const auto title = HtmlText(post.value(u"title"_q).toObject()
				.value(u"rendered"_q).toString());
			const auto relevance = Relevance(title, wanted);
			if (relevance < kTekstPesniRelevance
				|| relevance <= bestRelevance) {
				continue;
			}
			const auto html = post.value(u"content"_q).toObject()
				.value(u"rendered"_q).toString();
			const auto match = heading.match(html);
			if (!match.hasMatch()) {
				continue;
			}
			auto section = html.mid(match.capturedEnd());
			const auto end = next.match(section);
			if (end.hasMatch()) {
				section = section.left(end.capturedStart());
			}
			const auto text = HtmlText(section);
			if (text.isEmpty()) {
				continue;
			}
			best = ParsePlain(text);
			bestRelevance = relevance;
		}
		if (best.found) {
			LOG(("[lyrics] tekst-pesni found (relevance %1)"
				).arg(bestRelevance));
		}
		done(std::move(best));
	}, "https://tekst-pesni.com/");
}

void RequestTekstPesni(
		const QString &trackName,
		const QString &artistName,
		Fn<void(Lyrics)> done) {
	const auto wanted = (artistName.trimmed() + ' ' + trackName).trimmed();
	SearchTekstPesni(wanted, wanted, [=](Lyrics result) {
		if (result.found || trackName.size() < 4 || wanted == trackName) {
			done(std::move(result));
			return;
		}
		// The site names posts its own way: try the title alone, still
		// asking the post to carry most of the words of both.
		SearchTekstPesni(trackName, wanted, done);
	});
}

void RequestFallback(
		const QString &trackName,
		const QString &artistName,
		qint64 durationMs,
		Fn<void(Lyrics)> done,
		bool unavailable = false) {
	const auto netease = [=](bool failed) {
		RequestNetease(
			artistName + u" "_q + trackName,
			trackName,
			artistName,
			durationMs,
			[=](Lyrics result) {
				result.unavailable |= failed && !result.found;
				done(std::move(result));
			});
	};
	const auto kugou = [=](bool failed) {
		RequestKugou(trackName, artistName, durationMs, [=](Lyrics result) {
			if (result.found) {
				done(std::move(result));
			} else {
				netease(failed || result.unavailable);
			}
		});
	};
	const auto genius = [=](bool failed) {
		RequestGenius(trackName, artistName, [=](Lyrics result) {
			if (result.found) {
				done(std::move(result));
			} else {
				kugou(failed);
			}
		});
	};
	const auto tekstPesni = [=](bool failed) {
		RequestTekstPesni(trackName, artistName, [=](Lyrics result) {
			if (result.found) {
				done(std::move(result));
			} else {
				genius(failed);
			}
		});
	};
	if (durationMs <= 0 || unavailable) {
		tekstPesni(unavailable);
		return;
	}
	auto query = QUrlQuery();
	query.addQueryItem(u"track_name"_q, trackName);
	Request(query, durationMs, [=](Lyrics result) {
		if (result.found) {
			done(std::move(result));
		} else {
			tekstPesni(result.unavailable);
		}
	}, trackName);
}

} // namespace

namespace {

[[nodiscard]] QString NormalizedLine(const QString &text) {
	auto result = text.toLower();
	static const auto other = QRegularExpression(uR"([^\p{L}\p{N}]+)"_q);
	result.replace(other, u" "_q);
	return result.simplified();
}

// The KRC is for the same song when most of its lines are lines of the
// lyrics found before: Kugou may add a line with the names at the start.
[[nodiscard]] bool SameLyrics(const Lyrics &base, const Lyrics &words) {
	auto known = std::vector<QString>();
	for (const auto &line : base.lines) {
		const auto text = NormalizedLine(line.text);
		if (!text.isEmpty()) {
			known.push_back(text);
		}
	}
	auto total = 0;
	auto same = 0;
	for (const auto &line : words.lines) {
		const auto text = NormalizedLine(line.text);
		if (text.isEmpty()) {
			continue;
		}
		++total;
		if (std::find(known.begin(), known.end(), text) != known.end()) {
			++same;
		}
	}
	return (total > 0) && (same * 2 >= std::min(total, int(known.size())));
}

} // namespace

QString CleanTitle(const QString &title, const QString &artist) {
	auto result = title.trimmed();
	static const auto extension = QRegularExpression(
		uR"(\.(mp3|m4a|flac|ogg|oga|opus|wav|aac|aiff|aif|wma|alac)$)"_q,
		QRegularExpression::CaseInsensitiveOption);
	result.remove(extension);
	result.replace(QChar('_'), QChar(' '));
	static const auto tail = QRegularExpression(
		uR"(\s*[\(\[][^\)\]]*(official|video|audio|lyrics|clip|прем|клип|visualizer|hd|4k)[^\)\]]*[\)\]])"_q,
		QRegularExpression::CaseInsensitiveOption);
	result.remove(tail);
	const auto prefix = artist.trimmed() + u" - "_q;
	if (!artist.isEmpty() && result.startsWith(prefix, Qt::CaseInsensitive)) {
		result = result.mid(prefix.size());
	}
	return result.simplified();
}

namespace {

struct SplitResult {
	QString clean;
	QString trackName;
	QString artistName;
};

[[nodiscard]] SplitResult SplitName(const QString &title, const QString &artist) {
	auto result = SplitResult{ .clean = CleanTitle(title, QString()) };
	// "Исполнитель - Название" внутри заголовка: у загрузчика чужое имя.
	result.trackName = result.clean;
	result.artistName = artist.trimmed();
	static const auto separator = QRegularExpression(uR"(\s+[-–—]\s+)"_q);
	const auto dash = separator.match(result.clean);
	if (dash.hasMatch() && dash.capturedStart() > 0) {
		result.artistName = result.clean.left(dash.capturedStart()).trimmed();
		result.trackName = CleanTitle(
			result.clean.mid(dash.capturedEnd()),
			result.artistName);
	}
	result.trackName = WithoutBrackets(result.trackName);
	return result;
}

// Последняя попытка, когда исполнитель в метаданных чужой (загрузчик, а не автор):
// ищем по одному названию, но берём только точное название и близкую длительность.
void RequestLastResort(
		const SplitResult &split,
		qint64 durationMs,
		Fn<void(Lyrics)> done) {
	const auto name = split.trackName;
	if (name.size() < 4) {
		done({});
		return;
	}
	const auto netease = [=] {
		RequestNetease(name, name, split.artistName, durationMs, done, true);
	};
	auto byName = QUrlQuery();
	byName.addQueryItem(u"q"_q, name);
	Request(byName, durationMs, [=](Lyrics result) {
		if (result.found) {
			done(std::move(result));
			return;
		}
		// «Название - Исполнитель»: порядок мог быть обратным.
		static const auto separator = QRegularExpression(uR"(\s+[-–—]\s+)"_q);
		const auto dash = separator.match(split.clean);
		if (!dash.hasMatch() || dash.capturedStart() <= 0) {
			netease();
			return;
		}
		auto swapped = QUrlQuery();
		swapped.addQueryItem(
			u"track_name"_q,
			WithoutBrackets(CleanTitle(split.clean.left(dash.capturedStart()), QString())));
		swapped.addQueryItem(u"artist_name"_q, split.clean.mid(dash.capturedEnd()).trimmed());
		Request(swapped, durationMs, [=](Lyrics result) {
			if (result.found) {
				done(std::move(result));
			} else {
				netease();
			}
		});
	}, name);
}

} // namespace

void FetchWordTimings(
		const QString &title,
		const QString &artist,
		qint64 durationMs,
		const Lyrics &base,
		Fn<void(Lyrics)> done) {
	if (base.words || !base.synced || durationMs <= 0) {
		done({});
		return;
	}
	const auto split = SplitName(title, artist);
	RequestKugou(split.trackName, split.artistName, durationMs, [=](
			Lyrics result) {
		if (result.found && result.words && SameLyrics(base, result)) {
			done(std::move(result));
		} else {
			done({});
		}
	});
}

void FetchLyrics(
		const QString &title,
		const QString &artist,
		qint64 durationMs,
		Fn<void(Lyrics)> done) {
	const auto split = SplitName(title, artist);
	const auto clean = split.clean;
	const auto trackName = split.trackName;
	const auto artistName = split.artistName;
	const auto finish = [=](Lyrics result) {
		done(std::move(result));
	};
	auto first = QUrlQuery();
	first.addQueryItem(u"track_name"_q, trackName);
	first.addQueryItem(u"artist_name"_q, artistName);
	Request(first, durationMs, [=](Lyrics result) {
		if (result.found) {
			finish(std::move(result));
			return;
		}
		if (result.unavailable) {
			RequestFallback(trackName, artistName, durationMs, finish, true);
			return;
		}
		auto second = QUrlQuery();
		second.addQueryItem(u"q"_q, artistName + u" "_q + trackName);
		Request(second, durationMs, [=](Lyrics result) {
			if (result.found) {
				finish(std::move(result));
				return;
			}
			if (result.unavailable) {
				RequestFallback(trackName, artistName, durationMs, finish, true);
				return;
			}
			if (artistName == artist) {
				RequestFallback(trackName, artistName, durationMs, [=](Lyrics result) {
					if (result.found) {
						finish(std::move(result));
					} else {
						RequestLastResort(split, durationMs, finish);
					}
				});
				return;
			}
			auto third = QUrlQuery();
			third.addQueryItem(u"q"_q, clean);
			Request(third, durationMs, [=](Lyrics result) {
				if (result.found) {
					finish(std::move(result));
					return;
				}
				RequestFallback(trackName, artistName, durationMs, [=](Lyrics result) {
					if (result.found) {
						finish(std::move(result));
					} else {
						RequestLastResort(split, durationMs, finish);
					}
				}, result.unavailable);
			});
		});
	});
}

} // namespace HoursGram::Music::Sources
