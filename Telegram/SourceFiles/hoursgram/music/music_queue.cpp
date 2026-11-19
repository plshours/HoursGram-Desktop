/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_queue.h"

#include "base/random.h"
#include "base/unixtime.h"
#include "core/file_location.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "media/player/media_player_instance.h"

#include <QtCore/QFileInfo>

namespace HoursGram::Music {
namespace {

// A chart song that is missing from Telegram is skipped, but a whole run of
// them must not turn into an endless chain of searches.
constexpr auto kSkipAttempts = 5;

// Documents fabricated from downloaded SoundCloud files live under a fixed
// high prefix of the document id space, away from real Telegram ids.
constexpr auto kScDocumentPrefix = 0x4852530000000000ULL;

[[nodiscard]] bool SameTrack(const AudioMsgId &a, const AudioMsgId &b) {
	return a.audio()
		&& (a.audio() == b.audio())
		&& (a.contextId() == b.contextId());
}

[[nodiscard]] DocumentId ScDocumentId(const QString &trackId) {
	const auto id = trackId.toULongLong();
	if (!id || id >= (1ULL << 56)) {
		return 0;
	}
	return DocumentId(kScDocumentPrefix + id);
}

[[nodiscard]] not_null<DocumentData*> MakeScDocument(
		not_null<Main::Session*> session,
		const ScTrack &track,
		const QString &path) {
	auto attributes = QVector<MTPDocumentAttribute>();
	attributes.push_back(MTP_documentAttributeAudio(
		MTP_flags(MTPDdocumentAttributeAudio::Flag::f_title
			| MTPDdocumentAttributeAudio::Flag::f_performer),
		MTP_int(track.durationMs / 1000),
		MTP_string(track.title),
		MTP_string(track.artist),
		MTPstring()));
	attributes.push_back(MTP_documentAttributeFilename(
		MTP_string(track.title + '.' + track.extension)));
	const auto document = session->data().document(
		ScDocumentId(track.id),
		0,
		QByteArray(),
		base::unixtime::now(),
		attributes,
		track.extension == u"mp3"_q ? u"audio/mpeg"_q : u"audio/mp4"_q,
		InlineImageLocation(),
		ImageWithLocation(),
		ImageWithLocation(),
		false,
		0,
		QFileInfo(path).size());
	document->setLocation(Core::FileLocation(path));
	return document;
}

} // namespace

Queue &Queue::Instance() {
	static auto result = Queue();
	return result;
}

void Queue::start(
		not_null<Main::Session*> session,
		std::vector<QueueTrack> tracks,
		int index) {
	_session = base::make_weak(session);
	_tracks = std::move(tracks);
	_current = AudioMsgId();
	if (index < 0 || index >= int(_tracks.size())) {
		forget();
		return;
	}
	playAt(index, 1, kSkipAttempts);
}

void Queue::forget() {
	++_generation;
	_tracks.clear();
	_current = AudioMsgId();
	_index = -1;
	_resolving = false;
	_changes.fire({});
}

bool Queue::owns(const AudioMsgId &audio) const {
	return (_index >= 0) && SameTrack(audio, _current);
}

int Queue::stepFrom(int index, int delta, bool repeatAll) const {
	const auto count = int(_tracks.size());
	if (!count) {
		return -1;
	}
	const auto next = index + delta;
	if (next >= 0 && next < count) {
		return next;
	} else if (!repeatAll) {
		return -1;
	}
	return ((next % count) + count) % count;
}

bool Queue::available(int delta, bool repeatAll) const {
	return (_index >= 0) && (stepFrom(_index, delta, repeatAll) >= 0);
}

bool Queue::move(int delta, bool repeatAll, bool shuffle) {
	if (_index < 0 || _tracks.empty()) {
		return false;
	}
	auto next = -1;
	if (shuffle && delta > 0 && _tracks.size() > 1) {
		next = base::RandomIndex(int(_tracks.size()) - 1);
		if (next >= _index) {
			++next;
		}
	} else {
		next = stepFrom(_index, delta, repeatAll);
	}
	if (next < 0) {
		return false;
	}
	playAt(next, (delta < 0) ? -1 : 1, kSkipAttempts);
	return true;
}

void Queue::playAt(int index, int direction, int attemptsLeft) {
	const auto session = _session.get();
	if (!session || index < 0 || index >= int(_tracks.size())) {
		return;
	}
	_index = index;
	const auto generation = ++_generation;
	const auto &track = _tracks[index];
	if (track.resolved()) {
		_resolving = false;
		_current = track.audio;
		_changes.fire({});
		Media::Player::instance()->play(track.audio);
		prefetchAfter(index);
		return;
	}
	if (track.soundcloud) {
		_resolving = true;
		_changes.fire({});
		const auto sc = track.soundcloud;
		const auto start = [=](ScTrack resolved) {
			SoundCloudDownload(resolved, [=](
					const QString &path,
					const QString &error) {
				if (generation != _generation
					|| index >= int(_tracks.size())) {
					return;
				}
				_resolving = false;
				const auto session = _session.get();
				if (!session || !error.isEmpty()) {
					_notFound.fire_copy(_tracks[index].chart);
					_changes.fire({});
					return;
				}
				const auto document = MakeScDocument(
					session,
					resolved,
					path);
				const auto audio = AudioMsgId(document, FullMsgId());
				_tracks[index].audio = audio;
				_current = audio;
				_changes.fire({});
				Media::Player::instance()->play(audio);
				prefetchAfter(index);
			});
		};
		if (!sc->transcodingUrl.isEmpty()) {
			start(*sc);
		} else {
			SoundCloudResolve(*sc, [=](ScTrack resolved, const QString &error) {
				if (generation != _generation) {
					return;
				}
				if (!error.isEmpty()) {
					_resolving = false;
					_notFound.fire_copy(_tracks[index].chart);
					_changes.fire({});
					return;
				}
				start(resolved);
			});
		}
		return;
	}
	_resolving = true;
	_changes.fire({});
	ResolveChartTrack(session, track.chart, [=](AudioMsgId audio) {
		if (generation != _generation || index >= int(_tracks.size())) {
			return;
		}
		_resolving = false;
		if (audio) {
			_tracks[index].audio = audio;
			_current = audio;
			_changes.fire({});
			Media::Player::instance()->play(audio);
			prefetchAfter(index);
			return;
		}
		_notFound.fire_copy(_tracks[index].chart);
		if (attemptsLeft > 0) {
			const auto next = stepFrom(index, direction, false);
			if (next >= 0) {
				playAt(next, direction, attemptsLeft - 1);
				return;
			}
		}
		_changes.fire({});
	});
}

// The next song of the list is looked up and downloaded while this one
// plays, so a chart song missing from the cache does not make a pause
// between two tracks.
void Queue::prefetchAfter(int index) {
	const auto session = _session.get();
	const auto next = stepFrom(index, 1, false);
	if (!session || next < 0) {
		return;
	}
	const auto &track = _tracks[next];
	if (track.resolved()) {
		Prefetch(track.audio);
		return;
	}
	if (track.soundcloud) {
		return;
	}
	const auto generation = _generation;
	ResolveChartTrack(session, track.chart, [=](AudioMsgId audio) {
		if (!audio
			|| generation != _generation
			|| next >= int(_tracks.size())) {
			return;
		}
		if (!_tracks[next].resolved()) {
			_tracks[next].audio = audio;
		}
		Prefetch(_tracks[next].audio);
	});
}

rpl::producer<> Queue::changes() const {
	return _changes.events();
}

rpl::producer<ChartTrack> Queue::notFound() const {
	return _notFound.events();
}

} // namespace HoursGram::Music
