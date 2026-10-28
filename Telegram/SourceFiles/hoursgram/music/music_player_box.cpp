/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_player_box.h"

#include "base/unique_qptr.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "hoursgram/music/music_lyrics_loader.h"
#include "hoursgram/music/music_lyrics_online.h"
#include "hoursgram/music/music_playlists_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/effects/animations.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "styles/style_hoursgram_music.h"
#include "styles/style_layers.h"

#include <QtCore/QFile>

namespace HoursGram::Music {
namespace {

constexpr auto kType = AudioMsgId::Type::Song;
constexpr auto kScrollDuration = crl::time(350);

// Only one player window makes sense, so re-opening it raises the one that
// is already there instead of stacking a second copy on top.
base::unique_qptr<Ui::SeparatePanel> GlobalPlayerWindow;

[[nodiscard]] Media::Player::Instance *Player() {
	return Media::Player::instance();
}

// The artwork of the track that is playing. Album art rides along with the
// audio document as its thumbnail, so it may still be downloading when the
// track starts; until it arrives, or when the track has no artwork at all,
// the cover is a plain quiet tile with the player glyph in the middle. With
// nothing playing there is no cover: it takes no room.
class Cover final : public Ui::RpWidget {
public:
	explicit Cover(QWidget *parent);

	void setTrack(const AudioMsgId &audioId);
	void setIdle(bool idle);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void subscribeToDownloading(not_null<DocumentData*> document);
	void prepare();

	std::shared_ptr<Data::DocumentMedia> _media;
	rpl::lifetime _downloading;
	QPixmap _artwork;
	bool _blurred = false;
	bool _idle = false;

};

Cover::Cover(QWidget *parent) : RpWidget(parent) {
	resize(st::hoursgramPlayerCoverSize, st::hoursgramPlayerCoverSize);
}

void Cover::setIdle(bool idle) {
	if (_idle == idle) {
		return;
	}
	_idle = idle;
	if (_idle) {
		_media = nullptr;
		_downloading.destroy();
		_artwork = QPixmap();
		_blurred = false;
	}
	resize(width(), _idle ? 0 : st::hoursgramPlayerCoverSize);
	update();
}

void Cover::setTrack(const AudioMsgId &audioId) {
	_media = nullptr;
	_downloading.destroy();
	_artwork = QPixmap();
	_blurred = false;

	const auto document = audioId.audio();
	if (document && document->hasThumbnail()) {
		_media = document->createMediaView();
		_media->thumbnailWanted(Data::FileOriginMessage(audioId.contextId()));
		prepare();
		if (_blurred || _artwork.isNull()) {
			subscribeToDownloading(document);
		}
	}
	update();
}

void Cover::subscribeToDownloading(not_null<DocumentData*> document) {
	document->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		prepare();
		if (!_artwork.isNull() && !_blurred) {
			_downloading.destroy();
		}
		update();
	}, _downloading);
}

void Cover::prepare() {
	if (!_media) {
		return;
	}
	const auto full = _media->thumbnail();
	const auto source = full ? full : _media->thumbnailInline();
	if (!source) {
		return;
	} else if (!full && !_artwork.isNull()) {
		// Already showing the blurred preview, nothing better arrived yet.
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto size = QSize(
		st::hoursgramPlayerCoverSize,
		st::hoursgramPlayerCoverSize) * ratio;
	auto args = Images::PrepareArgs{ .outer = size };
	if (!full) {
		args = args.blurred();
	}
	auto image = Images::Prepare(source->original(), size, args);
	const auto masks = Images::CornersMask(st::hoursgramPlayerCoverRadius);
	image = Images::Round(std::move(image), masks);
	image.setDevicePixelRatio(ratio);
	_artwork = Ui::PixmapFromImage(std::move(image));
	_blurred = !full;
}

void Cover::paintEvent(QPaintEvent *e) {
	if (_idle) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto size = st::hoursgramPlayerCoverSize;
	const auto inner = QRect((width() - size) / 2, 0, size, size);
	if (!_artwork.isNull()) {
		p.drawPixmap(inner.topLeft(), _artwork);
		return;
	}

	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgRipple);
	p.drawRoundedRect(
		inner,
		st::hoursgramPlayerCoverRadius,
		st::hoursgramPlayerCoverRadius);

	const auto &icon = st::hoursgramPlayerCoverIcon;
	icon.paint(
		p,
		inner.x() + (size - icon.width()) / 2,
		inner.y() + (size - icon.height()) / 2,
		width());
}

// Draws the lyrics lines one under another, highlighting the line that the
// playback is inside of. The widget keeps a fixed height and scrolls its
// own content, so that the line in play stays in the middle of it.
class LyricsInner final : public Ui::RpWidget {
public:
	LyricsInner(QWidget *parent, int fixedHeight);

	void setLyrics(Lyrics lyrics);
	void setPosition(crl::time position);
	void setFixedHeight(int height);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Line {
		Ui::Text::String text;
		crl::time at = -1;
		int top = 0;
		int height = 0;
	};

	void setActive(int index);
	void scrollTo(int top);
	void paintPlaceholder(Painter &p);

	[[nodiscard]] int currentScrollTop() const;
	[[nodiscard]] int maxScrollTop() const;

	Lyrics _lyrics;
	std::vector<Line> _lines;
	Ui::Text::String _placeholder;
	Ui::Animations::Simple _scroll;
	int _fixedHeight = 0;
	int _contentHeight = 0;
	int _scrollTop = 0;
	int _active = -1;

};

LyricsInner::LyricsInner(QWidget *parent, int fixedHeight)
: RpWidget(parent)
, _fixedHeight(fixedHeight) {
	_placeholder.setText(
		st::hoursgramLyricsInactive,
		tr::hoursgram_LyricsEmpty(tr::now),
		Ui::NameTextOptions());
	resize(width(), fixedHeight);
}

void LyricsInner::setLyrics(Lyrics lyrics) {
	_lyrics = std::move(lyrics);
	_lines.clear();
	_lines.reserve(_lyrics.lines.size());
	for (const auto &line : _lyrics.lines) {
		auto entry = Line();
		entry.at = line.at;
		entry.text.setText(
			st::hoursgramLyricsActive,
			line.text,
			Ui::NameTextOptions());
		_lines.push_back(std::move(entry));
	}
	_active = -1;
	_scroll.stop();
	_scrollTop = 0;
	resizeToWidth(width());
	update();
}

// In the separate window the lyrics take whatever is left under the
// transport, so the window can be resized and the text follows it.
void LyricsInner::setFixedHeight(int height) {
	if (_fixedHeight == height) {
		return;
	}
	_fixedHeight = height;
	resizeToWidth(width());
	update();
}

void LyricsInner::setPosition(crl::time position) {
	setActive(_lyrics.lineAt(position));
}

void LyricsInner::setActive(int index) {
	if (_active == index) {
		return;
	}
	_active = index;
	update();
	if (_active >= 0 && _active < int(_lines.size())) {
		const auto &line = _lines[_active];
		scrollTo(line.top + (line.height / 2) - (height() / 2));
	}
}

int LyricsInner::currentScrollTop() const {
	return int(base::SafeRound(_scroll.value(_scrollTop)));
}

int LyricsInner::maxScrollTop() const {
	return std::max(_contentHeight - height(), 0);
}

void LyricsInner::scrollTo(int top) {
	const auto clamped = std::clamp(top, 0, maxScrollTop());
	if (clamped == _scrollTop) {
		return;
	}
	const auto from = currentScrollTop();
	_scrollTop = clamped;
	_scroll.stop();
	_scroll.start([=] { update(); }, from, clamped, kScrollDuration);
}

int LyricsInner::resizeGetHeight(int newWidth) {
	const auto padding = st::hoursgramLyricsPadding;
	const auto available = newWidth - padding.left() - padding.right();
	if (available <= 0) {
		return _fixedHeight;
	}
	auto top = padding.top();
	for (auto &line : _lines) {
		line.top = top;
		line.height = line.text.isEmpty()
			? st::hoursgramLyricsActive.font->height
			: line.text.countHeight(available);
		top += line.height + st::hoursgramLyricsLineSkip;
	}
	if (!_lines.empty()) {
		top -= st::hoursgramLyricsLineSkip;
	}
	_contentHeight = top + padding.bottom();
	_scrollTop = std::clamp(_scrollTop, 0, maxScrollTop());
	return _fixedHeight;
}

void LyricsInner::paintPlaceholder(Painter &p) {
	const auto padding = st::hoursgramLyricsPadding;
	const auto available = width() - padding.left() - padding.right();
	const auto textHeight = _placeholder.countHeight(available);
	p.setPen(st::windowSubTextFg);
	_placeholder.drawLeft(
		p,
		padding.left(),
		(height() - textHeight) / 2,
		available,
		width(),
		style::al_top);
}

void LyricsInner::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto padding = st::hoursgramLyricsPadding;
	const auto available = width() - padding.left() - padding.right();
	if (available <= 0) {
		return;
	} else if (_lines.empty()) {
		paintPlaceholder(p);
		return;
	}
	const auto shift = currentScrollTop();
	const auto clip = e->rect();
	const auto bottom = clip.y() + clip.height();
	for (auto i = 0, count = int(_lines.size()); i != count; ++i) {
		const auto &line = _lines[i];
		const auto top = line.top - shift;
		if (top + line.height < clip.y()) {
			continue;
		} else if (top > bottom) {
			break;
		} else if (line.text.isEmpty()) {
			continue;
		}
		const auto active = (i == _active);
		p.setOpacity(active ? 1. : 0.6);
		p.setPen(active ? st::windowBgActive : st::windowSubTextFg);
		line.text.drawLeft(
			p,
			padding.left(),
			top,
			available,
			width(),
			style::al_top);
	}
	p.setOpacity(1.);
}

// Builds the whole player: artwork, track name, seek bar, transport and the
// lyrics. The box and the separate window show the same widget, they only
// differ in how much room the lyrics get.
struct Content {
	object_ptr<Ui::VerticalLayout> widget;
	LyricsInner *lyrics = nullptr;
};

[[nodiscard]] Content CreateContent(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		int lyricsHeight) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto layout = result.data();

	struct State {
		DocumentId lyricsFor = 0;
		crl::time durationMs = 0;
		crl::time seekPositionMs = -1;
	};
	const auto state = layout->lifetime().make_state<State>();
	const auto padding = st::boxRowPadding;

	Ui::AddSkip(layout, st::hoursgramPlayerCoverSkip);
	const auto cover = layout->add(
		object_ptr<Cover>(layout),
		style::margins(0, 0, 0, st::hoursgramPlayerCoverSkip));

	const auto title = layout->add(
		object_ptr<Ui::FlatLabel>(layout, st::hoursgramPlayerTitle),
		padding);
	Ui::AddSkip(layout, st::hoursgramPlayerPerformerSkip);
	const auto performer = layout->add(
		object_ptr<Ui::FlatLabel>(layout, st::hoursgramPlayerPerformer),
		padding);

	Ui::AddSkip(layout, st::hoursgramPlayerSliderSkip);
	auto sliderOwned = object_ptr<Ui::MediaSlider>(
		layout,
		st::hoursgramPlayerSlider);
	sliderOwned->resize(
		st::hoursgramPlayerWidth,
		st::hoursgramPlayerSliderHeight);
	const auto slider = layout->add(std::move(sliderOwned), padding);

	Ui::AddSkip(layout, st::hoursgramPlayerTimeSkip);
	const auto times = layout->add(
		object_ptr<Ui::FixedHeightWidget>(
			layout,
			st::hoursgramPlayerTime.style.font->height),
		padding);
	const auto position = Ui::CreateChild<Ui::FlatLabel>(
		times,
		st::hoursgramPlayerTime);
	const auto duration = Ui::CreateChild<Ui::FlatLabel>(
		times,
		st::hoursgramPlayerTime);
	times->widthValue() | rpl::on_next([=](int width) {
		position->moveToLeft(0, 0, width);
		duration->moveToRight(0, 0, width);
	}, times->lifetime());
	duration->widthValue() | rpl::on_next([=] {
		duration->moveToRight(0, 0, times->width());
	}, duration->lifetime());

	Ui::AddSkip(layout, st::hoursgramPlayerControlsSkip);
	const auto controls = layout->add(
		object_ptr<Ui::FixedHeightWidget>(
			layout,
			st::hoursgramPlayerControlsHeight));
	const auto previous = Ui::CreateChild<Ui::IconButton>(
		controls,
		st::hoursgramPlayerPrevious);
	const auto play = Ui::CreateChild<Ui::IconButton>(
		controls,
		st::hoursgramPlayerPlay);
	const auto next = Ui::CreateChild<Ui::IconButton>(
		controls,
		st::hoursgramPlayerSide);
	controls->widthValue() | rpl::on_next([=](int width) {
		const auto space = st::hoursgramPlayerControlsSpace;
		const auto center = width / 2;
		play->moveToLeft(
			center - (play->width() / 2),
			(controls->height() - play->height()) / 2,
			width);
		previous->moveToLeft(
			center - (play->width() / 2) - space - previous->width(),
			(controls->height() - previous->height()) / 2,
			width);
		next->moveToLeft(
			center + (play->width() / 2) + space,
			(controls->height() - next->height()) / 2,
			width);
	}, controls->lifetime());
	controls->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(controls);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawEllipse(play->geometry());
	}, controls->lifetime());

	Ui::AddSkip(layout, st::hoursgramLyricsSkip);
	const auto lyrics = layout->add(
		object_ptr<LyricsInner>(layout, lyricsHeight));

	Ui::AddSkip(layout, st::hoursgramPlayerActionsSkip);
	const auto actions = layout->add(
		object_ptr<Ui::FixedHeightWidget>(
			layout,
			st::hoursgramPlayerAction.height));
	const auto attach = Ui::CreateChild<Ui::RoundButton>(
		actions,
		tr::hoursgram_LyricsAttach(),
		st::hoursgramPlayerActionSecondary);
	const auto playlist = Ui::CreateChild<Ui::RoundButton>(
		actions,
		tr::hoursgram_AddToPlaylist(),
		st::hoursgramPlayerAction);
	rpl::combine(
		actions->widthValue(),
		attach->widthValue(),
		playlist->widthValue()
	) | rpl::on_next([=](int width, int attachWidth, int playlistWidth) {
		const auto space = st::hoursgramPlayerActionsSpace;
		const auto total = attachWidth + space + playlistWidth;
		attach->moveToLeft((width - total) / 2, 0, width);
		playlist->moveToLeft((width + total) / 2 - playlistWidth, 0, width);
	}, actions->lifetime());
	Ui::AddSkip(layout, st::hoursgramPlayerActionsSkip);

	// Nothing on disk means the track has not been looked up yet, so the
	// lyrics database is asked once; the answer arrives later and only
	// counts while the same track is still the one on screen.
	const auto reloadLyrics = [=](DocumentData *document) {
		state->lyricsFor = document ? document->id : DocumentId();
		auto loaded = document ? LoadLyrics(document) : Lyrics();
		const auto found = !loaded.empty();
		lyrics->setLyrics(std::move(loaded));
		if (!document || found) {
			return;
		}
		const auto id = document->id;
		SearchLyricsOnline(document, crl::guard(layout, [=](Lyrics result) {
			if (state->lyricsFor == id) {
				lyrics->setLyrics(std::move(result));
			}
		}));
	};

	const auto updateTrack = [=](const AudioMsgId &audioId) {
		const auto document = audioId.audio();
		if (!document) {
			cover->setIdle(true);
			title->setText(tr::hoursgram_PlayerIdle(tr::now));
			performer->setText(QString());
			reloadLyrics(nullptr);
			return;
		}
		cover->setIdle(false);
		const auto song = document->song();
		const auto name = (song && !song->title.isEmpty())
			? song->title
			: document->filename();
		title->setText(name);
		performer->setText((song && !song->performer.isEmpty())
			? song->performer
			: tr::hoursgram_PlayerUnknownArtist(tr::now));
		if (state->lyricsFor != document->id) {
			cover->setTrack(audioId);
			reloadLyrics(document);
		}
	};

	const auto updateState = [=](const Media::Player::TrackState &track) {
		const auto frequency = track.frequency
			? track.frequency
			: Media::Player::kDefaultFrequency;
		state->durationMs = (track.length * 1000LL) / frequency;
		const auto positionMs = (track.position * 1000LL) / frequency;
		if (state->seekPositionMs < 0) {
			position->setText(Ui::FormatDurationText(positionMs / 1000));
			slider->setValue(state->durationMs
				? (float64(positionMs) / state->durationMs)
				: 0.);
			lyrics->setPosition(positionMs);
		}
		duration->setText(Ui::FormatDurationText(state->durationMs / 1000));
		slider->setDisabled(!state->durationMs);
		play->setIconOverride(
			Media::Player::ShowPauseIcon(track.state)
				? &st::hoursgramPlayerPauseIcon
				: nullptr);
	};

	slider->setChangeProgressCallback([=](float64 value) {
		if (!state->durationMs) {
			return;
		}
		state->seekPositionMs = crl::time(value * state->durationMs);
		position->setText(
			Ui::FormatDurationText(state->seekPositionMs / 1000));
		lyrics->setPosition(state->seekPositionMs);
		Player()->startSeeking(kType);
	});
	slider->setChangeFinishedCallback([=](float64 value) {
		state->seekPositionMs = -1;
		if (state->durationMs) {
			Player()->finishSeeking(kType, value);
		} else {
			Player()->cancelSeeking(kType);
		}
	});

	play->setClickedCallback([=] {
		Player()->playPause(kType);
	});
	previous->setClickedCallback([=] {
		Player()->previous(kType);
	});
	next->setClickedCallback([=] {
		Player()->next(kType);
	});

	attach->setClickedCallback([=] {
		const auto document = Player()->current(kType).audio();
		if (!document) {
			return;
		}
		const auto id = document->id;
		FileDialog::GetOpenPath(
			layout,
			tr::hoursgram_LyricsAttach(tr::now),
			tr::hoursgram_LyricsFilter(tr::now),
			crl::guard(layout, [=](FileDialog::OpenResult &&result) {
				auto bytes = result.remoteContent;
				if (bytes.isEmpty() && !result.paths.isEmpty()) {
					auto file = QFile(result.paths.front());
					if (file.open(QIODevice::ReadOnly)) {
						bytes = file.readAll();
					}
				}
				if (AttachLyrics(id, bytes)) {
					state->lyricsFor = 0;
					reloadLyrics(Player()->current(kType).audio());
				}
			}));
	});
	playlist->setClickedCallback([=] {
		const auto current = Player()->current(kType);
		if (current.audio()) {
			ShowAddToPlaylist(controller, current);
		}
	});

	Player()->updatedNotifier(
	) | rpl::filter([](const Media::Player::TrackState &track) {
		return (track.id.type() == kType);
	}) | rpl::on_next([=](const Media::Player::TrackState &track) {
		updateTrack(track.id);
		updateState(track);
	}, layout->lifetime());

	Player()->trackChanged(
	) | rpl::filter([](AudioMsgId::Type type) {
		return (type == kType);
	}) | rpl::on_next([=] {
		updateTrack(Player()->current(kType));
	}, layout->lifetime());

	updateTrack(Player()->current(kType));
	updateState(Player()->getState(kType));

	return { std::move(result), lyrics };
}

} // namespace

void ShowPlayer(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::hoursgram_PlayerTitle());
		box->setWidth(st::hoursgramPlayerWidth);
		box->verticalLayout()->add(
			CreateContent(
				box,
				controller,
				st::hoursgramLyricsHeight).widget);
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowPlayerWindow(
		not_null<Window::SessionController*> controller,
		const AudioMsgId &audioId) {
	if (audioId.audio() && !(Player()->current(kType) == audioId)) {
		Player()->play(audioId);
	}
	if (GlobalPlayerWindow) {
		GlobalPlayerWindow->showAndActivate();
		return;
	}
	GlobalPlayerWindow = base::make_unique_q<Ui::SeparatePanel>();
	const auto panel = GlobalPlayerWindow.get();
	panel->setTitle(tr::hoursgram_PlayerTitle());
	panel->setInnerSize(st::hoursgramPlayerWindowSize, true);
	panel->closeRequests() | rpl::on_next([=] {
		panel->hideGetDuration();
	}, panel->lifetime());
	panel->closeEvents() | rpl::on_next([] {
		crl::on_main([] {
			GlobalPlayerWindow = nullptr;
		});
	}, panel->lifetime());
	auto wrap = base::make_unique_q<Ui::RpWidget>(panel);
	const auto outer = wrap.get();
	auto content = CreateContent(
		outer,
		controller,
		st::hoursgramLyricsWindowHeight);
	const auto lyrics = content.lyrics;
	const auto layout = content.widget.release();
	layout->show();

	// The window is one column of fixed blocks plus the lyrics, so every
	// pixel the window gains or loses goes to the lyrics and nothing ends
	// up cut off at the bottom.
	outer->sizeValue() | rpl::on_next([=](QSize size) {
		if (size.isEmpty()) {
			return;
		}
		layout->resizeToWidth(size.width());
		const auto rest = layout->height() - lyrics->height();
		lyrics->setFixedHeight(std::max(
			size.height() - rest,
			st::hoursgramLyricsMinHeight));
		layout->resizeToWidth(size.width());
		layout->move(0, 0);
	}, outer->lifetime());

	panel->showInner(std::move(wrap));
	panel->showAndActivate();
}

} // namespace HoursGram::Music
