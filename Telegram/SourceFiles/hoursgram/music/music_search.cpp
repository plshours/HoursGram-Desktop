/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
/*
The music search is modeled after mokaGram, https://github.com/ecronxy/mokagram
(c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
of MOKAGRAM_NOTICE.md (section 7(b)).

All SoundCloud network interaction lives in music_soundcloud.cpp, which
carries the full SoundCloud usage notice.
*/
#include "hoursgram/music/music_search.h"

#include "base/algorithm.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_messages.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "hoursgram/music/music_soundcloud.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "mtproto/mtproto_response.h"
#include "storage/storage_shared_media.h"
#include "ui/image/image_location.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/text/format_song_document_name.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/pill_tabs.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/rp_window.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_hoursgram_music.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QContextMenuEvent>
#include <QtGui/QPainterPath>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>

#include <algorithm>

namespace HoursGram::Music {
namespace {

constexpr auto kType = AudioMsgId::Type::Song;
constexpr auto kTypingDelay = crl::time(400);
constexpr auto kMinQuerySize = 2;
constexpr auto kLoadMoreThreshold = 240;

// Documents fabricated from downloaded SoundCloud files live under a fixed
// high prefix of the document id space, away from real Telegram ids.
constexpr auto kDocumentPrefix = 0x4852530000000000ULL;

struct FoundTrack {
	QString id;
	QString title;
	QString artist;
	int durationMs = 0;
	QString permalink;
	QString artworkUrl;
	FullMsgId message;
	DocumentData *document = nullptr;
	std::shared_ptr<ScTrack> soundcloud;
};

[[nodiscard]] DocumentId ScDocumentId(const QString &trackId) {
	const auto id = trackId.toULongLong();
	if (!id || id >= (1ULL << 56)) {
		return 0;
	}
	return DocumentId(kDocumentPrefix + id);
}

[[nodiscard]] QString DurationText(int ms) {
	const auto seconds = ms / 1000;
	return u"%1:%2"_q.arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

[[nodiscard]] FoundTrack FoundFromScTrack(const ScTrack &track) {
	auto result = FoundTrack();
	result.id = track.id;
	result.title = track.title;
	result.artist = track.artist;
	result.durationMs = track.durationMs;
	result.permalink = track.permalink;
	result.artworkUrl = track.artworkUrl;
	result.soundcloud = std::make_shared<ScTrack>(track);
	return result;
}

[[nodiscard]] FoundTrack TrackFromMessage(not_null<HistoryItem*> item) {
	const auto document = item->media() ? item->media()->document() : nullptr;
	if (!document || !document->isSong()) {
		return {};
	}
	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	auto track = FoundTrack();
	const auto id = item->fullId();
	track.id = u"telegram:%1:%2"_q.arg(id.peer.value).arg(id.msg.bare);
	track.title = name.title;
	track.artist = name.performer;
	track.durationMs = document->duration();
	track.message = id;
	track.document = document;
	return track;
}

// A found SoundCloud track is played from a local file dressed up as a
// document: the media player only plays documents, so the downloaded file
// becomes its location and the audio attributes carry the title and the
// artist for the player panel.
[[nodiscard]] not_null<DocumentData*> MakeDocument(
		not_null<Main::Session*> session,
		const ScTrack &track,
		const QString &path) {
	const auto size = QFileInfo(path).size();
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
		size);
	document->setLocation(Core::FileLocation(path));
	return document;
}

class TelegramSearch final {
public:
	TelegramSearch(
		not_null<Main::Session*> session,
		Fn<void(std::vector<FoundTrack>, QString)> done)
	: _session(base::make_weak(session))
	, _done(std::move(done)) {
	}

	~TelegramSearch() {
		cancel();
	}

	void search(const QString &query) {
		cancel();
		_query = query;
		_tracks.clear();
		_offset = {};
		_rate = 0;
		_loaded = 0;
		_more = true;
		searchMore();
	}

	void searchMore() {
		const auto session = _session.get();
		if (!session || _requestId || !_more) {
			return;
		}
		const auto generation = _generation;
		const auto received = [=](const MTPmessages_Messages &result) {
			if (generation != _generation) {
				return;
			}
			_requestId = 0;
			if (result.type() == mtpc_messages_messagesNotModified) {
				_more = false;
				_done(_tracks, QString());
				return;
			}
			const auto parsed = Api::ParseGlobalMediaResult(session, result);
			_loaded += int(parsed.messageIds.size());
			const auto last = parsed.messageIds.empty()
				? _offset
				: parsed.messageIds.back();
			_more = !parsed.messageIds.empty()
				&& _loaded < parsed.fullCount
				&& (last != _offset || parsed.offsetRate != _rate);
			_offset = last;
			_rate = parsed.offsetRate;
			for (const auto &position : parsed.messageIds) {
				if (const auto item = session->data().message(
						position.fullId)) {
					auto track = TrackFromMessage(item);
					if (track.id.isEmpty()) {
						continue;
					}
					const auto duplicate = std::find_if(
						_tracks.begin(),
						_tracks.end(),
						[&](const FoundTrack &row) {
							return row.id == track.id;
						});
					if (duplicate == _tracks.end()) {
						_tracks.push_back(std::move(track));
					}
				}
			}
			_done(_tracks, QString());
		};
		const auto failed = [=](const MTP::Error &error) {
			if (generation != _generation) {
				return;
			}
			_requestId = 0;
			_more = false;
			_done(_tracks, MTP::IgnoreError(error) ? QString() : error.type());
		};
		const auto request = Api::PrepareGlobalMediaRequest(
			session,
			_rate,
			_offset,
			Storage::SharedMediaType::MusicFile,
			_query);
		if (!request) {
			_more = false;
			return;
		}
		_requestId = session->api().request(*request
		).done(received).fail(failed).send();
	}

	[[nodiscard]] bool hasMore() const {
		return _more && !_requestId;
	}

	void cancel() {
		++_generation;
		if (const auto session = _session.get()) {
			session->api().request(base::take(_requestId)).cancel();
		}
		_more = false;
	}

private:
	base::weak_ptr<Main::Session> _session;
	Fn<void(std::vector<FoundTrack>, QString)> _done;
	QString _query;
	std::vector<FoundTrack> _tracks;
	Data::MessagePosition _offset;
	int32 _rate = 0;
	int _loaded = 0;
	mtpRequestId _requestId = 0;
	int _generation = 0;
	bool _more = false;

};

class TrackRow;
void ShowMessageJump(FullMsgId fullId);

struct State {
	base::unique_qptr<Ui::RpWindow> window;
	Ui::InputField *field = nullptr;
	Ui::PillTabs *sources = nullptr;
	Ui::ScrollArea *scroll = nullptr;
	Ui::VerticalLayout *list = nullptr;
	base::weak_ptr<Window::SessionController> controller;
	std::unique_ptr<TelegramSearch> telegram;
	std::unique_ptr<base::Timer> typing;
	std::vector<FoundTrack> results;
	std::vector<TrackRow*> rows;
	QString query;
	bool soundCloud = false;
	bool loading = false;
	bool more = false;
	int generation = 0;
	int nextOffset = 0;
	rpl::lifetime lifetime;
};

class TrackRow final : public Ui::RippleButton {
public:
	TrackRow(QWidget *parent, FoundTrack track)
	: Ui::RippleButton(parent, st::hoursgramPlaylistRipple)
	, _track(std::move(track)) {
		resize(parent->width(), st::hoursgramPlaylistRowHeight);
	}

	void setPlaying(bool playing) {
		if (_playing == playing) {
			return;
		}
		_playing = playing;
		update();
	}

	void setArtwork(QImage artwork) {
		if (artwork.isNull()) {
			return;
		}
		const auto size = st::hoursgramPlaylistCoverSize;
		_artwork = artwork.scaled(
			size * style::DevicePixelRatio(),
			size * style::DevicePixelRatio(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		update();
	}

	[[nodiscard]] const FoundTrack &track() const {
		return _track;
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		if (isOver() || isDown()) {
			p.fillRect(e->rect(), st::windowBgOver);
		}
		paintRipple(p, 0, 0);
		paintTile(p);

		const auto left = st::hoursgramPlaylistRowPadding.left()
			+ st::hoursgramPlaylistCoverSize
			+ st::hoursgramPlaylistCoverSkip;
		const auto available = width()
			- left
			- st::hoursgramSearchDurationWidth
			- st::hoursgramPlaylistRowPadding.right();
		if (available <= 0) {
			return;
		}
		p.setPen(_playing ? st::windowActiveTextFg : st::windowBoldFg);
		p.setFont(st::hoursgramPlaylistName.font);
		p.drawTextLeft(
			left,
			st::hoursgramPlaylistNameTop,
			width(),
			st::hoursgramPlaylistName.font->elided(
				_track.title.isEmpty()
					? tr::hoursgram_PlayerUnknownTrack(tr::now)
					: _track.title,
				available));

		p.setPen(st::windowSubTextFg);
		p.setFont(st::hoursgramPlaylistStatus.font);
		p.drawTextLeft(
			left,
			st::hoursgramPlaylistStatusTop,
			width(),
			st::hoursgramPlaylistStatus.font->elided(
				_track.artist.isEmpty()
					? tr::hoursgram_PlayerUnknownArtist(tr::now)
					: _track.artist,
				available));

		p.setFont(st::hoursgramSearchDuration.font);
		const auto text = DurationText(_track.durationMs);
		p.drawTextLeft(
			width()
				- st::hoursgramSearchDurationWidth
				- st::hoursgramPlaylistRowPadding.right(),
			st::hoursgramPlaylistNameTop + 2,
			width(),
			text);
	}

	void contextMenuEvent(QContextMenuEvent *e) override {
		if (!_track.message.peer && _track.permalink.isEmpty()) {
			return;
		}
		auto menu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		if (_track.message.peer) {
			menu->addAction(
				tr::hoursgram_MusicSearchShowMessage(tr::now),
				[=] {
					ShowMessageJump(_track.message);
				},
				&st::menuIconShowInChat);
		}
		if (!_track.permalink.isEmpty()) {
			menu->addAction(
				tr::hoursgram_MusicSearchOpenInBrowser(tr::now),
				[=] {
					File::OpenUrl(_track.permalink);
				},
				&st::menuIconShare);
		}
		menu->popup(e->globalPos());
	}

private:
	void paintTile(QPainter &p) {
		const auto size = st::hoursgramPlaylistCoverSize;
		auto path = QPainterPath();
		path.addRoundedRect(
			QRectF(0, 0, size, size),
			st::hoursgramPlaylistCoverRadius,
			st::hoursgramPlaylistCoverRadius);
		p.save();
		p.setClipPath(path);
		if (_artwork.isNull()) {
			p.fillRect(QRect(0, 0, size, size), st::windowBgRipple);
			st::hoursgramPlaylistTileIcon.paint(
				p,
				(size - st::hoursgramPlaylistTileIcon.width()) / 2,
				(size - st::hoursgramPlaylistTileIcon.height()) / 2,
				size);
		} else {
			p.drawImage(QRect(0, 0, size, size), _artwork);
		}
		p.restore();
	}

	FoundTrack _track;
	bool _playing = false;
	QImage _artwork;

};

State &Shared() {
	static auto state = State();
	return state;
}

[[nodiscard]] bool QueryTooShort(const QString &query) {
	return query.size() < kMinQuerySize;
}

void ShowMessageJump(FullMsgId fullId) {
	const auto controller = Shared().controller.get();
	if (!controller) {
		return;
	}
	if (const auto item = controller->session().data().message(fullId)) {
		controller->showPeerHistory(item->history(), {}, item->id);
	}
}

void RefreshPlaying() {
	auto &state = Shared();
	const auto current = Media::Player::instance()->current(kType);
	const auto playing = current.audio()
		? current.audio()->id
		: DocumentId(0);
	for (auto index = 0; index < int(state.rows.size()); ++index) {
		const auto &track = state.results[index];
		state.rows[index]->setPlaying(playing != 0
			&& ((track.document && track.document->id == playing)
				|| (!track.message.peer && ScDocumentId(track.id) == playing)));
	}
}

void PlayRow(int index) {
	auto &state = Shared();
	const auto controller = state.controller.get();
	if (!controller
		|| index < 0
		|| index >= int(state.results.size())) {
		return;
	}
	const auto &track = state.results[index];
	if (track.message.peer && track.document) {
		Media::Player::instance()->play(
			AudioMsgId(track.document, track.message));
		RefreshPlaying();
		return;
	}
	if (!track.soundcloud) {
		return;
	}
	controller->showToast(tr::hoursgram_MusicSearchLoading(
		tr::now,
		lt_track,
		track.title));
	const auto generation = state.generation;
	const auto known = track.soundcloud;
	const auto start = [=](ScTrack resolved) {
		SoundCloudDownload(resolved, [=](
				const QString &path,
				const QString &error) {
			const auto controller = Shared().controller.get();
			if (generation != Shared().generation || !controller) {
				return;
			}
			if (!error.isEmpty()) {
				controller->showToast(error);
				return;
			}
			const auto document = MakeDocument(
				&controller->session(),
				resolved,
				path);
			Media::Player::instance()->play(
				AudioMsgId(document, FullMsgId()));
			RefreshPlaying();
		});
	};
	if (!known->transcodingUrl.isEmpty()) {
		start(*known);
		return;
	}
	SoundCloudResolve(*known, [=](ScTrack resolved, const QString &error) {
		if (generation != Shared().generation) {
			return;
		}
		const auto controller = Shared().controller.get();
		if (!error.isEmpty()) {
			if (controller) {
				controller->showToast(error);
			}
			return;
		}
		start(resolved);
	});
}

[[nodiscard]] bool NearBottom() {
	auto &state = Shared();
	if (!state.scroll || state.results.empty()) {
		return false;
	}
	return state.scroll->scrollTop()
		+ state.scroll->height()
		+ style::ConvertScale(kLoadMoreThreshold)
		>= state.list->height();
}

void LoadMore();

void ApplyResults(std::vector<FoundTrack> tracks, QString error) {
	auto &state = Shared();
	const auto controller = state.controller.get();
	state.loading = false;
	if (!error.isEmpty()) {
		if (controller) {
			controller->showToast(std::move(error));
		}
		return;
	}
	if (tracks.empty()) {
		state.more = false;
	} else {
		auto &existing = state.results;
		for (auto &track : tracks) {
			const auto duplicate = std::find_if(
				existing.begin(),
				existing.end(),
				[&](const FoundTrack &row) {
					return row.id == track.id;
				});
			if (duplicate == existing.end()) {
				existing.push_back(std::move(track));
			}
		}
	}
	RebuildRows();
	if (state.more && !state.loading && NearBottom()) {
		LoadMore();
	}
}

void LoadMore() {
	auto &state = Shared();
	if (state.loading || QueryTooShort(state.query)) {
		return;
	}
	const auto generation = state.generation;
	state.loading = true;
	if (state.soundCloud) {
		SoundCloudSearch(state.query, state.nextOffset, [=](
				ScSearchResult result) {
			if (generation != Shared().generation) {
				return;
			}
			auto &state = Shared();
			state.more = result.nextOffset > 0;
			state.nextOffset = result.nextOffset;
			auto tracks = std::vector<FoundTrack>();
			tracks.reserve(result.tracks.size());
			for (const auto &track : result.tracks) {
				tracks.push_back(FoundFromScTrack(track));
			}
			ApplyResults(std::move(tracks), result.error);
		});
	} else {
		state.telegram->searchMore();
	}
}

void RunSearch() {
	auto &state = Shared();
	const auto query = state.field->getLastText().trimmed();
	++state.generation;
	const auto generation = state.generation;
	state.telegram->cancel();
	state.more = false;
	state.nextOffset = 0;
	state.loading = false;
	state.query = query;
	state.results.clear();
	RebuildRows();
	if (QueryTooShort(query)) {
		return;
	}
	state.loading = true;
	if (state.soundCloud) {
		SoundCloudSearch(query, 0, [=](ScSearchResult result) {
			if (generation != Shared().generation) {
				return;
			}
			auto &state = Shared();
			state.more = result.nextOffset > 0;
			state.nextOffset = result.nextOffset;
			auto tracks = std::vector<FoundTrack>();
			tracks.reserve(result.tracks.size());
			for (const auto &track : result.tracks) {
				tracks.push_back(FoundFromScTrack(track));
			}
			ApplyResults(std::move(tracks), result.error);
		});
	} else {
		state.telegram->search(query);
	}
}

void RebuildRows() {
	auto &state = Shared();
	state.rows.clear();
	state.list->clear();
	if (state.results.empty()) {
		state.list->add(
			object_ptr<Ui::FlatLabel>(
				state.list,
				state.loading
					? tr::hoursgram_MusicSearchSearching()
					: (QueryTooShort(state.query)
						? tr::hoursgram_MusicSearchHint()
						: tr::hoursgram_MusicSearchNoResults()),
				st::hoursgramPlaylistEmpty),
			st::hoursgramPlaylistEmptyPadding);
	} else {
		for (auto index = 0; index < int(state.results.size()); ++index) {
			const auto found = state.results[index];
			const auto row = state.list->add(
				object_ptr<TrackRow>(state.list, found));
			state.rows.push_back(row);
			row->setClickedCallback([=] {
				PlayRow(index);
			});
			if (!found.artworkUrl.isEmpty()) {
				const auto url = found.artworkUrl;
				SoundCloudLoadImage(url, [weak = QPointer<TrackRow>(row)](
						QImage image) {
					if (weak) {
						weak->setArtwork(std::move(image));
					}
				});
			}
		}
	}
	state.list->resizeToWidth(state.scroll->width());
	RefreshPlaying();
}

void SetupWindow(not_null<Window::SessionController*> controller) {
	auto &state = Shared();
	state.controller = base::make_weak(controller);
	state.window = base::make_unique_q<Ui::RpWindow>();
	const auto window = state.window.get();
	window->setTitle(tr::hoursgram_MusicSearch(tr::now));
	window->setFixedSize(st::hoursgramSearchWindowSize);
	const auto body = window->body();
	body->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(body);
		p.fillRect(clip, st::boxBg);
	}, body->lifetime());

	state.field = Ui::CreateChild<Ui::InputField>(
		body,
		st::defaultInputField,
		tr::hoursgram_MusicSearchPlaceholder());
	state.sources = Ui::CreateChild<Ui::PillTabs>(
		body,
		std::vector<QString>{
			tr::hoursgram_MusicSearchTelegram(tr::now),
			tr::hoursgram_MusicSearchSoundCloud(tr::now),
		},
		0,
		st::hoursgramSearchSources);
	state.scroll = Ui::CreateChild<Ui::ScrollArea>(
		body,
		st::defaultScrollArea);
	state.list = state.scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(state.scroll));

	state.typing = std::make_unique<base::Timer>(RunSearch);

	state.field->changes() | rpl::on_next([=] {
		Shared().typing->callOnce(kTypingDelay);
	}, state.lifetime);

	state.field->submits() | rpl::on_next([=] {
		Shared().typing->cancel();
		RunSearch();
	}, state.lifetime);

	state.sources->activeIndexChanges() | rpl::on_next([=](int index) {
		auto &state = Shared();
		const auto next = (index == 1);
		if (state.soundCloud != next) {
			state.soundCloud = next;
			RunSearch();
		}
	}, state.lifetime);

	state.scroll->scrolls() | rpl::on_next([=] {
		auto &state = Shared();
		if (state.more && !state.loading && NearBottom()) {
			LoadMore();
		}
	}, state.lifetime);

	Media::Player::instance()->updatedNotifier(
	) | rpl::on_next([=](const Media::Player::TrackState &state) {
		RefreshPlaying();
	}, state.lifetime);

	body->sizeValue() | rpl::on_next([=](QSize size) {
		auto &state = Shared();
		const auto padding = st::hoursgramSearchPadding;
		const auto width = size.width()
			- padding.left()
			- padding.right();
		state.field->setGeometry(
			padding.left(),
			padding.top(),
			width,
			st::hoursgramSearchFieldHeight);
		state.sources->setGeometry(
			padding.left(),
			state.field->bottomNoMargins() + st::hoursgramSearchSliderSkip,
			width,
			st::hoursgramSearchSources.height);
		const auto listTop = state.sources->bottomNoMargins()
			+ st::hoursgramSearchSliderSkip;
		state.scroll->setGeometry(
			0,
			listTop,
			size.width(),
			size.height() - listTop);
		state.list->resizeToWidth(state.scroll->width());
		state.scroll->updateBars();
	}, body->lifetime());

	state.telegram = std::make_unique<TelegramSearch>(
		&controller->session(),
		[=](std::vector<FoundTrack> tracks, QString error) {
			ApplyResults(std::move(tracks), error);
		});

	window->show();
	state.field->setFocusFast();
}

} // namespace

void ShowSearch(not_null<Window::SessionController*> controller) {
	auto &state = Shared();
	if (state.window) {
		state.window->showNormal();
		state.window->raise();
		state.window->activateWindow();
		return;
	}
	SetupWindow(controller);
	RebuildRows();
}

} // namespace HoursGram::Music
