/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_window.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "hoursgram/music/music_common.h"
#include "hoursgram/music/music_lyrics_loader.h"
#include "hoursgram/music/music_lyrics_online.h"
#include "hoursgram/music/music_playlists.h"
#include "hoursgram/music/music_playlists_box.h"
#include "hoursgram/music/music_queue.h"
#include "hoursgram/music/music_sources.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/show.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include "styles/style_hoursgram_music.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QFile>
#include <QtCore/QLocale>
#include <QtGui/QCursor>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainterPath>

#include <cmath>

namespace HoursGram::Music {
namespace {

constexpr auto kType = AudioMsgId::Type::Song;
constexpr auto kSlideDuration = crl::time(280);
constexpr auto kHoverDuration = crl::time(170);
constexpr auto kSelectDuration = crl::time(260);
constexpr auto kAppearDuration = crl::time(340);
constexpr auto kAppearStagger = crl::time(26);
constexpr auto kAppearMaxDelay = crl::time(380);
constexpr auto kPopDuration = crl::time(320);
constexpr auto kCoverDuration = crl::time(420);
constexpr auto kLyricsScrollDuration = crl::time(450);
constexpr auto kLyricsManualPause = crl::time(3500);
constexpr auto kLyricsWaitOnline = crl::time(8000);
constexpr auto kSearchDelay = crl::time(450);
constexpr auto kNewBadgeDays = 14;
constexpr auto kMaxFreshCards = 24;
constexpr auto kHeroCovers = 4;
constexpr auto kPi = 3.14159265358979323846;

[[nodiscard]] Media::Player::Instance *Player() {
	return Media::Player::instance();
}

[[nodiscard]] QColor CardColor() {
	return anim::color(st::windowBg, st::windowBgOver, 0.55);
}

[[nodiscard]] int SectionOrder(Section section, uint64 playlistId) {
	switch (section) {
	case Section::Home: return 0;
	case Section::Search: return 1;
	case Section::Saved: return 2;
	case Section::NowPlaying: return 3;
	case Section::Playlist: return 4 + int(playlistId % 100000);
	}
	return 0;
}

// How a track of a list relates to what the player does right now. A chart
// song has no file until the queue finds one, so it counts as the current
// one while the queue looks it up and after it starts playing.
struct PlayState {
	bool current = false;
	bool playing = false;
	bool loading = false;

	friend inline bool operator==(PlayState, PlayState) = default;
};

[[nodiscard]] bool SameChart(const ChartTrack &a, const ChartTrack &b) {
	return !a.title.isEmpty()
		&& (a.title == b.title)
		&& (a.artist == b.artist);
}

[[nodiscard]] PlayState ComputeState(const QueueTrack &track) {
	auto result = PlayState();
	const auto current = Player()->current(kType);
	const auto &queue = Queue::Instance();
	const auto index = queue.index();
	const auto queued = (index >= 0 && index < int(queue.tracks().size()))
		? &queue.tracks()[index]
		: nullptr;
	if (track.resolved()) {
		result.current = (current.audio() == track.audio.audio())
			&& (current.contextId() == track.audio.contextId());
	} else if (queued && SameChart(queued->chart, track.chart)) {
		result.loading = queue.resolving();
		result.current = result.loading
			|| (queued->resolved()
				&& (current.audio() == queued->audio.audio()));
	}
	if (result.current && !result.loading) {
		const auto state = Player()->getState(kType);
		result.playing = (state.id == current)
			&& Media::Player::ShowPauseIcon(state.state);
	}
	return result;
}

void ActivateTrack(
		not_null<Main::Session*> session,
		std::vector<QueueTrack> list,
		int index) {
	if (index < 0 || index >= int(list.size())) {
		return;
	}
	const auto state = ComputeState(list[index]);
	if (state.loading) {
		return;
	} else if (state.current) {
		Player()->playPause(kType);
		return;
	}
	Queue::Instance().start(session, std::move(list), index);
}

[[nodiscard]] QString FormatReleased(const QDate &date) {
	if (!date.isValid()) {
		return QString();
	}
	const auto locale = UsesRussian()
		? QLocale(QLocale::Russian)
		: QLocale(QLocale::English);
	return locale.toString(date, u"d MMM"_q);
}

[[nodiscard]] bool IsNew(const ChartTrack &track) {
	return track.released.isValid()
		&& (track.released >= QDate::currentDate().addDays(-kNewBadgeDays));
}

[[nodiscard]] QString TitleOf(const QueueTrack &track) {
	return track.resolved()
		? TrackTitle(track.audio.audio())
		: track.chart.title;
}

[[nodiscard]] QString PerformerOf(const QueueTrack &track) {
	return track.resolved()
		? TrackPerformer(track.audio.audio())
		: track.chart.artist;
}

[[nodiscard]] QueueTrack FromAudio(const AudioMsgId &audio) {
	return { .audio = audio };
}

[[nodiscard]] QueueTrack FromChart(const ChartTrack &chart) {
	return { .chart = chart };
}

void PaintEqualizer(
		QPainter &p,
		QRect rect,
		const QColor &color,
		bool animated) {
	constexpr auto kBars = 4;
	const auto bar = st::hoursgramMusicRowEqWidth;
	const auto skip = st::hoursgramMusicRowEqSkip;
	const auto full = st::hoursgramMusicRowEqHeight;
	const auto total = kBars * bar + (kBars - 1) * skip;
	auto x = rect.x() + (rect.width() - total) / 2;
	const auto bottom = rect.y() + (rect.height() + full) / 2;
	const auto now = crl::now();
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	for (auto i = 0; i != kBars; ++i) {
		const auto phase = animated
			? (0.5 + 0.5 * std::sin(now / 130. + i * 1.9))
			: (0.3 + 0.15 * i);
		const auto height = std::max(
			float64(bar),
			full * (0.2 + 0.8 * phase));
		p.drawRoundedRect(
			QRectF(x, bottom - height, bar, height),
			bar / 2.,
			bar / 2.);
		x += bar + skip;
	}
}

// Cover art of a track: the thumbnail the audio file carries, or the image
// of the chart entry. Prepared once for the size it is drawn at, so a long
// list does not scale images on every repaint.
class Artwork final : public base::has_weak_ptr {
public:
	explicit Artwork(Fn<void()> repaint);

	void setTrack(const QueueTrack &track);
	void clear();

	[[nodiscard]] bool hasImage() const;
	[[nodiscard]] QImage original() const;
	void paint(
		QPainter &p,
		QRect rect,
		int radius,
		const style::icon &fallback,
		QColor fallbackBg);

private:
	void setDocument(not_null<DocumentData*> document, FullMsgId origin);
	void setUrl(const QString &url);
	void prepareThumbnail();
	void setOriginal(QImage image, bool blurred);

	Fn<void()> _repaint;
	std::shared_ptr<Data::DocumentMedia> _media;
	DocumentData *_document = nullptr;
	QString _url;
	QImage _original;
	bool _blurred = false;
	QPixmap _cache;
	QSize _cacheSize;
	int _cacheRadius = -1;
	rpl::lifetime _downloading;

};

Artwork::Artwork(Fn<void()> repaint) : _repaint(std::move(repaint)) {
}

void Artwork::clear() {
	_media = nullptr;
	_document = nullptr;
	_url = QString();
	_downloading.destroy();
	setOriginal(QImage(), false);
}

void Artwork::setTrack(const QueueTrack &track) {
	if (const auto document = track.audio.audio()) {
		if (_document != document) {
			clear();
			setDocument(document, track.audio.contextId());
		}
		if (_original.isNull() && !track.chart.artworkUrl.isEmpty()) {
			setUrl(track.chart.artworkUrl);
		}
	} else if (_url != track.chart.artworkUrl || _document) {
		clear();
		setUrl(track.chart.artworkUrl);
	}
}

void Artwork::setDocument(
		not_null<DocumentData*> document,
		FullMsgId origin) {
	_document = document;
	if (!document->hasThumbnail()) {
		return;
	}
	_media = document->createMediaView();
	_media->thumbnailWanted(Data::FileOriginMessage(origin));
	prepareThumbnail();
	if (_blurred || _original.isNull()) {
		document->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			prepareThumbnail();
			if (!_original.isNull() && !_blurred) {
				_downloading.destroy();
			}
		}, _downloading);
	}
}

void Artwork::setUrl(const QString &url) {
	_url = url;
	if (url.isEmpty()) {
		return;
	}
	LoadArtwork(url, crl::guard(this, [=](QImage image) {
		if (_url == url && (_original.isNull() || _blurred)) {
			setOriginal(std::move(image), false);
		}
	}));
}

void Artwork::prepareThumbnail() {
	if (!_media) {
		return;
	}
	const auto full = _media->thumbnail();
	const auto source = full ? full : _media->thumbnailInline();
	if (!source || (!full && !_original.isNull())) {
		return;
	}
	setOriginal(source->original(), !full);
}

void Artwork::setOriginal(QImage image, bool blurred) {
	_original = std::move(image);
	_blurred = blurred;
	_cache = QPixmap();
	_cacheSize = QSize();
	if (_repaint) {
		_repaint();
	}
}

bool Artwork::hasImage() const {
	return !_original.isNull();
}

QImage Artwork::original() const {
	return _original;
}

void Artwork::paint(
		QPainter &p,
		QRect rect,
		int radius,
		const style::icon &fallback,
		QColor fallbackBg) {
	if (rect.isEmpty()) {
		return;
	} else if (_original.isNull()) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(fallbackBg);
		p.drawRoundedRect(rect, radius, radius);
		fallback.paint(
			p,
			rect.x() + (rect.width() - fallback.width()) / 2,
			rect.y() + (rect.height() - fallback.height()) / 2,
			rect.x() + rect.width());
		return;
	}
	if (_cache.isNull()
		|| _cacheSize != rect.size()
		|| _cacheRadius != radius) {
		const auto ratio = style::DevicePixelRatio();
		const auto size = rect.size() * ratio;
		const auto scaled = _original.size().scaled(
			size,
			Qt::KeepAspectRatioByExpanding);
		auto args = Images::PrepareArgs{ .outer = size };
		if (_blurred) {
			args = args.blurred();
		}
		auto image = Images::Prepare(_original, scaled, args);
		image = Images::Round(std::move(image), Images::CornersMask(radius));
		image.setDevicePixelRatio(ratio);
		_cache = Ui::PixmapFromImage(std::move(image));
		_cacheSize = rect.size();
		_cacheRadius = radius;
	}
	p.drawPixmap(rect.topLeft(), _cache);
}

// Rows and cards fade in one after another when a page is built, which
// makes a freshly loaded list feel like it arrives rather than pops up.
class Appearing {
public:
	void appear(not_null<QWidget*> widget, crl::time delay) {
		_pending = true;
		base::call_delayed(delay, widget.get(), [=] {
			_pending = false;
			_animation.start(
				[=] { widget->update(); },
				0.,
				1.,
				kAppearDuration,
				anim::easeOutCubic);
		});
	}
	[[nodiscard]] float64 value() const {
		return _pending ? 0. : _animation.value(1.);
	}

private:
	Ui::Animations::Simple _animation;
	bool _pending = false;

};

class TrackRow final : public Ui::RippleButton {
public:
	TrackRow(QWidget *parent, QueueTrack track, int number);

	[[nodiscard]] const QueueTrack &track() const {
		return _track;
	}
	void setTrack(QueueTrack track);
	void refreshState();
	void appear(crl::time delay);
	void setMenuCallback(Fn<void()> callback);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QImage prepareRippleMask() const override;

private:
	QueueTrack _track;
	int _number = 0;
	QString _title;
	QString _performer;
	QString _side;
	Artwork _artwork;
	object_ptr<Ui::IconButton> _menu;
	Ui::Animations::Simple _over;
	Ui::Animations::Basic _equalizer;
	Appearing _appearing;
	PlayState _state;
	bool _wasOver = false;

};

TrackRow::TrackRow(QWidget *parent, QueueTrack track, int number)
: RippleButton(parent, st::hoursgramPlaylistRipple)
, _number(number)
, _artwork([=] { update(); })
, _menu(this, st::hoursgramPlaylistMenu) {
	resize(width(), st::hoursgramMusicRowHeight);
	_equalizer.init([=] { update(); });
	_menu->hide();
	setTrack(std::move(track));
}

void TrackRow::setTrack(QueueTrack track) {
	_track = std::move(track);
	_title = TitleOf(_track);
	_performer = PerformerOf(_track);
	if (const auto document = _track.audio.audio()) {
		const auto seconds = document->duration() / 1000;
		_side = (seconds > 0) ? Ui::FormatDurationText(seconds) : QString();
	} else {
		_side = FormatReleased(_track.chart.released);
	}
	_artwork.setTrack(_track);
	refreshState();
	update();
}

void TrackRow::setMenuCallback(Fn<void()> callback) {
	_menu->setClickedCallback(std::move(callback));
	_menu->show();
}

void TrackRow::appear(crl::time delay) {
	_appearing.appear(this, delay);
	update();
}

void TrackRow::refreshState() {
	const auto state = ComputeState(_track);
	if (_state == state) {
		return;
	}
	_state = state;
	if (_state.playing || _state.loading) {
		if (!_equalizer.animating()) {
			_equalizer.start();
		}
	} else {
		_equalizer.stop();
	}
	update();
}

void TrackRow::resizeEvent(QResizeEvent *e) {
	_menu->moveToRight(
		st::hoursgramPlaylistMenuSkip,
		(height() - _menu->height()) / 2,
		width());
}

void TrackRow::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	const auto over = isOver();
	if (_wasOver != over) {
		_wasOver = over;
		_over.start(
			[=] { update(); },
			over ? 0. : 1.,
			over ? 1. : 0.,
			kHoverDuration);
	}
}

QImage TrackRow::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::hoursgramMusicRowRadius);
}

void TrackRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto appear = _appearing.value();
	if (appear <= 0.) {
		return;
	} else if (appear < 1.) {
		p.setOpacity(appear);
		p.translate(0, (1. - appear) * st::hoursgramMusicSlideShift / 2.);
	}

	const auto over = _over.value(isOver() ? 1. : 0.);
	const auto radius = st::hoursgramMusicRowRadius;
	if (over > 0. || _state.current) {
		auto hq = PainterHighQualityEnabler(p);
		auto color = st::windowBgOver->c;
		if (!_state.current) {
			color.setAlphaF(color.alphaF() * over);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(rect(), radius, radius);
	}
	paintRipple(p, 0, 0);

	const auto padding = st::hoursgramMusicRowPadding;
	auto left = padding.left();
	const auto index = QRect(
		left,
		0,
		st::hoursgramMusicRowIndexWidth,
		height());
	if (_state.current) {
		PaintEqualizer(
			p,
			index,
			st::windowActiveTextFg->c,
			_state.playing || _state.loading);
	} else if (_number > 0) {
		p.setPen(st::windowSubTextFg);
		p.setFont(st::hoursgramMusicRowIndex);
		p.drawText(index, QString::number(_number), style::al_center);
	}
	left += index.width();

	const auto size = st::hoursgramMusicRowCover;
	const auto cover = QRect(left, (height() - size) / 2, size, size);
	if (_state.loading) {
		p.setOpacity(p.opacity() * (0.55 + 0.45 * std::sin(crl::now() / 160.)));
	}
	_artwork.paint(
		p,
		cover,
		st::hoursgramMusicRowCoverRadius,
		st::hoursgramMusicRowCoverIcon,
		st::windowBgRipple->c);
	p.setOpacity(appear);
	left += size + st::hoursgramMusicRowCoverSkip;

	const auto menu = _menu->isHidden() ? 0 : _menu->width();
	const auto sideWidth = _side.isEmpty()
		? 0
		: st::hoursgramMusicRowDurationWidth;
	const auto right = padding.right() + menu + sideWidth;
	const auto available = width() - left - right;
	if (available <= 0) {
		return;
	}
	p.setPen(_state.current ? st::windowActiveTextFg : st::windowBoldFg);
	p.setFont(st::hoursgramMusicRowTitle);
	p.drawTextLeft(
		left,
		st::hoursgramMusicRowTitleTop,
		width(),
		st::hoursgramMusicRowTitle->elided(_title, available));
	p.setPen(st::windowSubTextFg);
	p.setFont(st::hoursgramMusicRowSub);
	p.drawTextLeft(
		left,
		st::hoursgramMusicRowSubTop,
		width(),
		st::hoursgramMusicRowSub->elided(_performer, available));
	if (sideWidth) {
		p.drawText(
			QRect(
				width() - padding.right() - menu - sideWidth,
				0,
				sideWidth,
				height()),
			_side,
			style::al_center);
	}
}

// A song of the fresh wave: a big cover that lifts under the cursor and
// shows a play button, with the name and the artist under it.
class ChartCard final : public Ui::RippleButton {
public:
	ChartCard(QWidget *parent, QueueTrack track);

	void refreshState();
	void appear(crl::time delay);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QImage prepareRippleMask() const override;

private:
	[[nodiscard]] QRect coverRect() const;

	QueueTrack _track;
	Artwork _artwork;
	Ui::Animations::Simple _over;
	Ui::Animations::Basic _equalizer;
	Appearing _appearing;
	PlayState _state;
	bool _wasOver = false;
	bool _new = false;

};

ChartCard::ChartCard(QWidget *parent, QueueTrack track)
: RippleButton(parent, st::hoursgramPlaylistRipple)
, _track(std::move(track))
, _artwork([=] { update(); })
, _new(IsNew(_track.chart)) {
	_equalizer.init([=] { update(); });
	_artwork.setTrack(_track);
	refreshState();
}

void ChartCard::appear(crl::time delay) {
	_appearing.appear(this, delay);
	update();
}

void ChartCard::refreshState() {
	const auto state = ComputeState(_track);
	if (_state == state) {
		return;
	}
	_state = state;
	if (_state.playing || _state.loading) {
		if (!_equalizer.animating()) {
			_equalizer.start();
		}
	} else {
		_equalizer.stop();
	}
	update();
}

void ChartCard::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	const auto over = isOver();
	if (_wasOver != over) {
		_wasOver = over;
		_over.start(
			[=] { update(); },
			over ? 0. : 1.,
			over ? 1. : 0.,
			kHoverDuration,
			anim::easeOutCubic);
	}
}

QRect ChartCard::coverRect() const {
	return QRect(0, 0, width(), width());
}

QImage ChartCard::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		coverRect().size(),
		st::hoursgramMusicCardRadius2);
}

void ChartCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto appear = _appearing.value();
	if (appear <= 0.) {
		return;
	} else if (appear < 1.) {
		p.setOpacity(appear);
		p.translate(0, (1. - appear) * st::hoursgramMusicSlideShift);
	}
	const auto over = _over.value(isOver() ? 1. : 0.);
	const auto radius = st::hoursgramMusicCardRadius2;
	const auto lift = int(base::SafeRound(over * st::hoursgramMusicGap / 2.));
	const auto cover = coverRect().translated(0, -lift);

	if (over > 0.) {
		auto hq = PainterHighQualityEnabler(p);
		auto shadow = st::windowShadowFg->c;
		shadow.setAlphaF(0.35 * over);
		p.setPen(Qt::NoPen);
		p.setBrush(shadow);
		p.drawRoundedRect(
			cover.translated(0, st::hoursgramMusicGap),
			radius,
			radius);
	}
	_artwork.paint(
		p,
		cover,
		radius,
		st::hoursgramPlayerCoverIcon,
		st::windowBgRipple->c);
	p.save();
	p.translate(0, -lift);
	paintRipple(p, 0, 0);
	p.restore();

	auto hq = PainterHighQualityEnabler(p);
	if (_new) {
		const auto text = Tr("NEW", "НОВОЕ");
		const auto &font = st::hoursgramMusicCardBadge;
		const auto skip = st::hoursgramMusicRowEqSkip * 4;
		const auto badge = QRect(
			cover.x() + skip * 2,
			cover.y() + skip * 2,
			font->width(text) + skip * 4,
			font->height + skip * 2);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawRoundedRect(badge, badge.height() / 2., badge.height() / 2.);
		p.setPen(st::activeButtonFg);
		p.setFont(font);
		p.drawText(badge, text, style::al_center);
	}

	const auto showPlay = std::max(over, _state.current ? 1. : 0.);
	if (showPlay > 0.) {
		const auto size = st::hoursgramMusicCardPlaySize;
		const auto skip = st::hoursgramMusicGap;
		const auto scale = 0.7 + 0.3 * showPlay;
		const auto center = QPointF(
			cover.x() + cover.width() - skip - size / 2.,
			cover.y() + cover.height() - skip - size / 2.);
		p.save();
		p.setOpacity(p.opacity() * showPlay);
		p.translate(center);
		p.scale(scale, scale);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawEllipse(QRectF(-size / 2., -size / 2., size, size));
		if (_state.current && (_state.playing || _state.loading)) {
			PaintEqualizer(
				p,
				QRect(-size / 2, -size / 2, size, size),
				st::activeButtonFg->c,
				true);
		} else {
			const auto &icon = st::hoursgramMusicCardPlayIcon;
			icon.paint(
				p,
				-icon.width() / 2 + st::hoursgramMusicRowEqSkip,
				-icon.height() / 2,
				size);
		}
		p.restore();
	}

	const auto textTop = cover.y() + cover.height() + st::hoursgramMusicGap;
	p.setPen(_state.current ? st::windowActiveTextFg : st::windowBoldFg);
	p.setFont(st::hoursgramMusicCardTitle);
	p.drawTextLeft(
		0,
		textTop,
		width(),
		st::hoursgramMusicCardTitle->elided(_track.chart.title, width()));
	p.setPen(st::windowSubTextFg);
	p.setFont(st::hoursgramMusicCardSub);
	p.drawTextLeft(
		0,
		textTop + st::hoursgramMusicCardTitle->height + 2,
		width(),
		st::hoursgramMusicCardSub->elided(_track.chart.artist, width()));
}

// Lays the cards out in as many columns as fit, stretching them a little
// so that the right edge of the last column lines up with the page.
class CardsGrid final : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	void add(not_null<ChartCard*> card) {
		_cards.push_back(card);
	}
	[[nodiscard]] const std::vector<not_null<ChartCard*>> &cards() const {
		return _cards;
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto skip = st::hoursgramMusicCardSkip;
		const auto wanted = st::hoursgramMusicCardWidth;
		const auto columns = std::max((newWidth + skip) / (wanted + skip), 1);
		const auto cardWidth = std::max(
			(newWidth - (columns - 1) * skip) / columns,
			1);
		const auto textHeight = st::hoursgramMusicGap
			+ st::hoursgramMusicCardTitle->height
			+ st::hoursgramMusicCardSub->height
			+ st::hoursgramMusicGap;
		const auto cardHeight = cardWidth + textHeight;
		auto index = 0;
		for (const auto &card : _cards) {
			const auto row = index / columns;
			const auto column = index % columns;
			card->setGeometry(
				column * (cardWidth + skip),
				row * (cardHeight + skip),
				cardWidth,
				cardHeight);
			++index;
		}
		const auto rows = (int(_cards.size()) + columns - 1) / columns;
		return rows ? (rows * cardHeight + (rows - 1) * skip) : 0;
	}

private:
	std::vector<not_null<ChartCard*>> _cards;

};

// Grey shapes in the place of the rows while something loads, softly
// pulsing so that the wait reads as progress.
class Skeleton final : public Ui::RpWidget {
public:
	Skeleton(QWidget *parent, int rows)
	: RpWidget(parent)
	, _rows(rows) {
		_animation.init([=] { update(); });
		_animation.start();
		resize(width(), _rows * st::hoursgramMusicRowHeight);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return _rows * st::hoursgramMusicRowHeight;
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		const auto padding = st::hoursgramMusicRowPadding;
		const auto cover = st::hoursgramMusicRowCover;
		const auto left = padding.left() + st::hoursgramMusicRowIndexWidth;
		const auto textLeft = left + cover + st::hoursgramMusicRowCoverSkip;
		const auto line = st::hoursgramMusicRowTitle->height / 2;
		const auto now = crl::now();
		for (auto i = 0; i != _rows; ++i) {
			const auto top = i * st::hoursgramMusicRowHeight;
			const auto phase = 0.5 + 0.5 * std::sin(now / 260. - i * 0.6);
			auto color = st::windowBgOver->c;
			color.setAlphaF(color.alphaF() * (0.45 + 0.55 * phase));
			p.setBrush(color);
			p.drawRoundedRect(
				QRect(
					left,
					top + (st::hoursgramMusicRowHeight - cover) / 2,
					cover,
					cover),
				st::hoursgramMusicRowCoverRadius,
				st::hoursgramMusicRowCoverRadius);
			const auto widthTitle = std::max(width() / 3 - i * 7, line * 4);
			const auto widthSub = std::max(width() / 5 + i * 5, line * 3);
			p.drawRoundedRect(
				QRect(
					textLeft,
					top + st::hoursgramMusicRowTitleTop + line / 2,
					widthTitle,
					line),
				line / 2.,
				line / 2.);
			p.drawRoundedRect(
				QRect(
					textLeft,
					top + st::hoursgramMusicRowSubTop + line / 2,
					widthSub,
					line),
				line / 2.,
				line / 2.);
		}
	}

private:
	int _rows = 0;
	Ui::Animations::Basic _animation;

};

// A page title with a quieter line under it.
class PageHeader final : public Ui::RpWidget {
public:
	PageHeader(QWidget *parent, QString title, QString about, bool big)
	: RpWidget(parent)
	, _title(std::move(title))
	, _about(std::move(about))
	, _big(big) {
		resize(width(), computeHeight());
	}

	void setTexts(QString title, QString about) {
		_title = std::move(title);
		_about = std::move(about);
		resizeToWidth(width());
		update();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return computeHeight();
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		const auto &font = _big
			? st::hoursgramMusicPageTitle
			: st::hoursgramMusicSectionTitle;
		p.setPen(st::windowBoldFg);
		p.setFont(font);
		p.drawText(0, font->ascent, font->elided(_title, width()));
		if (!_about.isEmpty()) {
			p.setPen(st::windowSubTextFg);
			p.setFont(st::hoursgramMusicPageAbout);
			p.drawText(
				0,
				font->height
					+ st::hoursgramMusicPageAboutSkip
					+ st::hoursgramMusicPageAbout->ascent,
				st::hoursgramMusicPageAbout->elided(_about, width()));
		}
	}

private:
	[[nodiscard]] int computeHeight() const {
		const auto &font = _big
			? st::hoursgramMusicPageTitle
			: st::hoursgramMusicSectionTitle;
		return font->height + (_about.isEmpty()
			? 0
			: (st::hoursgramMusicPageAboutSkip
				+ st::hoursgramMusicPageAbout->height));
	}

	QString _title;
	QString _about;
	bool _big = false;

};

// The banner on top of the home page: the accent fading into the card
// color, with the covers of the newest songs fanned out on the right. They
// spread a little further under the cursor.
class HeroBanner final : public Ui::RpWidget {
public:
	HeroBanner(QWidget *parent, std::vector<ChartTrack> covers);

	[[nodiscard]] rpl::producer<> playRequests() const {
		return _playRequests.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	std::vector<std::unique_ptr<Artwork>> _covers;
	object_ptr<Ui::RoundButton> _play;
	Ui::Animations::Simple _spread;
	Ui::Animations::Simple _shown;
	rpl::event_stream<> _playRequests;
	bool _over = false;

};

HeroBanner::HeroBanner(QWidget *parent, std::vector<ChartTrack> covers)
: RpWidget(parent)
, _play(
	this,
	rpl::single(Tr("Play the wave", "Слушать волну")),
	st::hoursgramMusicActionSecondary) {
	resize(width(), st::hoursgramMusicHeroHeight);
	for (const auto &track : covers) {
		if (int(_covers.size()) >= kHeroCovers) {
			break;
		}
		auto artwork = std::make_unique<Artwork>([=] { update(); });
		artwork->setTrack(FromChart(track));
		_covers.push_back(std::move(artwork));
	}
	_play->setClickedCallback([=] {
		_playRequests.fire({});
	});
	_play->show();
	_shown.start(
		[=] { update(); },
		0.,
		1.,
		kCoverDuration * 2,
		anim::easeOutCubic);
}

void HeroBanner::resizeEvent(QResizeEvent *e) {
	const auto padding = st::hoursgramMusicHeroPadding;
	_play->moveToLeft(
		padding.left(),
		height() - padding.bottom() - _play->height(),
		width());
}

void HeroBanner::enterEventHook(QEnterEvent *e) {
	_over = true;
	_spread.start(
		[=] { update(); },
		0.,
		1.,
		kCoverDuration,
		anim::easeOutCubic);
}

void HeroBanner::leaveEventHook(QEvent *e) {
	_over = false;
	_spread.start(
		[=] { update(); },
		1.,
		0.,
		kCoverDuration,
		anim::easeOutCubic);
}

void HeroBanner::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::hoursgramMusicHeroRadius;
	auto gradient = QLinearGradient(0, 0, width(), height());
	gradient.setColorAt(0., st::windowBgActive->c);
	gradient.setColorAt(
		1.,
		anim::color(st::windowBgActive, CardColor(), 0.85));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(rect(), radius, radius);

	const auto padding = st::hoursgramMusicHeroPadding;
	p.setPen(st::activeButtonFg);
	p.setFont(st::hoursgramMusicHeroTitle);
	p.drawText(
		padding.left(),
		padding.top() + st::hoursgramMusicHeroTitle->ascent,
		Tr("Fresh wave", "Свежая волна"));
	p.setFont(st::hoursgramMusicHeroAbout);
	p.setOpacity(0.8);
	p.drawText(
		padding.left(),
		padding.top()
			+ st::hoursgramMusicHeroTitle->height
			+ st::hoursgramMusicPageAboutSkip
			+ st::hoursgramMusicHeroAbout->ascent,
		Tr(
			"New songs that everybody listens to right now",
			"Новинки, которые слушают прямо сейчас"));
	p.setOpacity(1.);

	const auto shown = _shown.value(1.);
	const auto spread = _spread.value(_over ? 1. : 0.);
	const auto size = st::hoursgramMusicHeroCoverSize;
	const auto shift = st::hoursgramMusicHeroCoverShift
		* (0.85 + 0.25 * spread);
	const auto count = int(_covers.size());
	const auto right = width() - padding.right();
	const auto top = (height() - size) / 2;
	for (auto i = count; i != 0;) {
		--i;
		const auto progress = std::clamp(shown * count - i, 0., 1.);
		if (progress <= 0.) {
			continue;
		}
		const auto x = right - size - (count - 1 - i) * shift;
		const auto angle = (i - (count - 1) / 2.) * 4. * (0.5 + spread);
		p.save();
		p.setOpacity(progress);
		p.translate(x + size / 2., top + size / 2. + (1. - progress) * 20.);
		p.rotate(angle);
		_covers[i]->paint(
			p,
			QRect(-size / 2, -size / 2, size, size),
			st::hoursgramMusicCardRadius2,
			st::hoursgramPlayerCoverIcon,
			st::windowBgRipple->c);
		p.restore();
	}
}

// The lyrics of the track in play, one line under another. The line the
// playback is inside of is highlighted and kept in the middle; a click on
// a timed line seeks to it, and scrolling by hand holds the view for a few
// seconds before it follows the playback again.
class LyricsView final : public Ui::RpWidget {
public:
	explicit LyricsView(QWidget *parent);

	void setLyrics(Lyrics lyrics);
	void setPlaceholder(const QString &text);
	void setPosition(crl::time position);
	void setSeekCallback(Fn<void(crl::time)> callback);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	struct Line {
		Ui::Text::String text;
		crl::time at = -1;
		int top = 0;
		int height = 0;
	};

	void relayout();
	void setActive(int index);
	void scrollTo(int top, bool animated);
	[[nodiscard]] int currentScrollTop() const;
	[[nodiscard]] int maxScrollTop() const;
	[[nodiscard]] int lineAt(QPoint point) const;

	std::vector<Line> _lines;
	Ui::Text::String _placeholder;
	Ui::Animations::Simple _scroll;
	Ui::Animations::Simple _activeChange;
	Fn<void(crl::time)> _seek;
	crl::time _manualUntil = 0;
	int _contentHeight = 0;
	int _scrollTop = 0;
	int _active = -1;
	int _previous = -1;

};

LyricsView::LyricsView(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);
}

void LyricsView::setPlaceholder(const QString &text) {
	_placeholder.setText(
		st::hoursgramLyricsInactive,
		text,
		Ui::NameTextOptions());
	update();
}

void LyricsView::setSeekCallback(Fn<void(crl::time)> callback) {
	_seek = std::move(callback);
}

void LyricsView::setLyrics(Lyrics lyrics) {
	_lines.clear();
	_lines.reserve(lyrics.lines.size());
	for (const auto &line : lyrics.lines) {
		auto entry = Line();
		entry.at = line.at;
		entry.text.setText(
			st::hoursgramMusicLyricsActive,
			line.text,
			Ui::NameTextOptions());
		_lines.push_back(std::move(entry));
	}
	_active = _previous = -1;
	_scroll.stop();
	_scrollTop = 0;
	_manualUntil = 0;
	relayout();
	update();
}

void LyricsView::resizeEvent(QResizeEvent *e) {
	relayout();
}

void LyricsView::relayout() {
	const auto padding = st::hoursgramMusicLyricsPadding;
	const auto available = width() - padding.left() - padding.right();
	if (available <= 0) {
		return;
	}
	auto top = std::max(padding.top(), height() / 3);
	for (auto &line : _lines) {
		line.top = top;
		line.height = line.text.isEmpty()
			? st::hoursgramMusicLyricsActive.font->height
			: line.text.countHeight(available);
		top += line.height + st::hoursgramMusicLyricsLineSkip;
	}
	_contentHeight = top + std::max(padding.bottom(), height() / 2);
	_scrollTop = std::clamp(_scrollTop, 0, maxScrollTop());
}

void LyricsView::setPosition(crl::time position) {
	auto index = -1;
	for (auto i = 0, count = int(_lines.size()); i != count; ++i) {
		if (_lines[i].at >= 0 && _lines[i].at <= position) {
			index = i;
		} else if (_lines[i].at > position) {
			break;
		}
	}
	setActive(index);
}

void LyricsView::setActive(int index) {
	if (_active == index) {
		return;
	}
	_previous = _active;
	_active = index;
	_activeChange.stop();
	_activeChange.start([=] { update(); }, 0., 1., kLyricsScrollDuration);
	if (_active >= 0
		&& _active < int(_lines.size())
		&& crl::now() >= _manualUntil) {
		const auto &line = _lines[_active];
		scrollTo(line.top + line.height / 2 - height() / 2, true);
	}
	update();
}

int LyricsView::currentScrollTop() const {
	return int(base::SafeRound(_scroll.value(_scrollTop)));
}

int LyricsView::maxScrollTop() const {
	return std::max(_contentHeight - height(), 0);
}

void LyricsView::scrollTo(int top, bool animated) {
	const auto clamped = std::clamp(top, 0, maxScrollTop());
	if (clamped == _scrollTop && !_scroll.animating()) {
		return;
	}
	const auto from = currentScrollTop();
	_scrollTop = clamped;
	_scroll.stop();
	if (animated) {
		_scroll.start(
			[=] { update(); },
			from,
			clamped,
			kLyricsScrollDuration,
			anim::easeOutCubic);
	}
	update();
}

void LyricsView::wheelEvent(QWheelEvent *e) {
	if (_lines.empty()) {
		return;
	}
	_manualUntil = crl::now() + kLyricsManualPause;
	scrollTo(currentScrollTop() - e->angleDelta().y() / 2, false);
	e->accept();
}

int LyricsView::lineAt(QPoint point) const {
	const auto y = point.y() + currentScrollTop();
	for (auto i = 0, count = int(_lines.size()); i != count; ++i) {
		const auto &line = _lines[i];
		if (y >= line.top && y < line.top + line.height) {
			return (line.at >= 0) ? i : -1;
		}
	}
	return -1;
}

void LyricsView::mouseMoveEvent(QMouseEvent *e) {
	setCursor((_seek && lineAt(e->pos()) >= 0)
		? style::cur_pointer
		: style::cur_default);
}

void LyricsView::mousePressEvent(QMouseEvent *e) {
	const auto index = lineAt(e->pos());
	if (index >= 0 && _seek) {
		_manualUntil = 0;
		_seek(_lines[index].at);
	}
}

void LyricsView::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto padding = st::hoursgramMusicLyricsPadding;
	const auto available = width() - padding.left() - padding.right();
	if (available <= 0) {
		return;
	} else if (_lines.empty()) {
		const auto textHeight = _placeholder.countHeight(available);
		p.setPen(st::windowSubTextFg);
		_placeholder.drawLeft(
			p,
			padding.left(),
			(height() - textHeight) / 2,
			available,
			width(),
			style::al_top);
		return;
	}
	const auto shift = currentScrollTop();
	const auto change = _activeChange.value(1.);
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
		const auto activeness = (i == _active)
			? change
			: (i == _previous)
			? (1. - change)
			: 0.;
		const auto passed = (_active >= 0 && i < _active);
		p.setOpacity(0.32 + 0.68 * activeness - (passed ? 0.08 : 0.));
		p.setPen(anim::color(
			st::windowSubTextFg,
			st::windowBoldFg,
			activeness));
		line.text.drawLeft(
			p,
			padding.left(),
			top,
			available,
			width(),
			style::al_left);
	}
	p.setOpacity(1.);

	const auto fade = padding.top();
	const auto bg = st::windowBg->c;
	auto transparent = bg;
	transparent.setAlpha(0);
	auto upper = QLinearGradient(0, 0, 0, fade);
	upper.setColorAt(0., bg);
	upper.setColorAt(1., transparent);
	p.fillRect(0, 0, width(), fade, upper);
	auto lower = QLinearGradient(0, height() - fade, 0, height());
	lower.setColorAt(0., transparent);
	lower.setColorAt(1., bg);
	p.fillRect(0, height() - fade, width(), fade, lower);
}

// Holds the page on screen. A new page slides in over the old one, both
// drawn from snapshots, and only then becomes a live widget again.
class PageStack final : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	void showPage(object_ptr<Ui::RpWidget> page, int direction);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void finish();

	object_ptr<Ui::RpWidget> _page = { nullptr };
	QPixmap _old;
	QPixmap _new;
	Ui::Animations::Simple _slide;
	int _direction = 0;

};

void PageStack::showPage(object_ptr<Ui::RpWidget> page, int direction) {
	const auto animated = _page
		&& !size().isEmpty()
		&& !anim::Disabled();
	_old = animated ? Ui::GrabWidget(_page.data()) : QPixmap();
	_page = std::move(page);
	_page->setGeometry(rect());
	_direction = direction;
	if (!animated) {
		_page->show();
		return;
	}
	_new = Ui::GrabWidget(_page.data());
	_page->hide();
	_slide.stop();
	_slide.start(
		[=] {
			update();
			if (!_slide.animating()) {
				finish();
			}
		},
		0.,
		1.,
		kSlideDuration,
		anim::easeOutCubic);
	update();
}

void PageStack::finish() {
	_old = _new = QPixmap();
	if (_page) {
		_page->show();
	}
	update();
}

void PageStack::resizeEvent(QResizeEvent *e) {
	if (_page) {
		_page->setGeometry(rect());
	}
}

void PageStack::paintEvent(QPaintEvent *e) {
	if (_old.isNull() && _new.isNull()) {
		return;
	}
	auto p = QPainter(this);
	const auto progress = _slide.value(1.);
	const auto shift = st::hoursgramMusicSlideShift;
	const auto direction = (_direction == 0) ? 1 : _direction;
	if (!_old.isNull()) {
		p.setOpacity(1. - progress);
		p.drawPixmap(
			QPointF(0, -direction * shift * progress),
			_old);
	}
	if (!_new.isNull()) {
		p.setOpacity(progress);
		p.drawPixmap(
			QPointF(0, direction * shift * (1. - progress)),
			_new);
	}
}

// One item of the side menu. The parent draws the moving highlight under
// the items, so an item only blends its text and icon towards the colors
// that stand on the accent.
class NavButton final : public Ui::RippleButton {
public:
	NavButton(
		QWidget *parent,
		QString text,
		const style::icon &icon,
		const style::icon &iconActive);

	void setText(const QString &text);
	void setCount(int count);
	void setActive(float64 progress);
	void setPlaying(bool playing);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QImage prepareRippleMask() const override;

private:
	QString _text;
	QString _count;
	const style::icon &_icon;
	const style::icon &_iconActive;
	Ui::Animations::Simple _over;
	Ui::Animations::Basic _equalizer;
	float64 _active = 0.;
	bool _wasOver = false;
	bool _playing = false;

};

NavButton::NavButton(
	QWidget *parent,
	QString text,
	const style::icon &icon,
	const style::icon &iconActive)
: RippleButton(parent, st::hoursgramPlaylistRipple)
, _text(std::move(text))
, _icon(icon)
, _iconActive(iconActive) {
	resize(width(), st::hoursgramMusicNavHeight);
	_equalizer.init([=] { update(); });
}

void NavButton::setText(const QString &text) {
	if (_text != text) {
		_text = text;
		update();
	}
}

void NavButton::setCount(int count) {
	const auto text = count ? QString::number(count) : QString();
	if (_count != text) {
		_count = text;
		update();
	}
}

void NavButton::setActive(float64 progress) {
	if (_active != progress) {
		_active = progress;
		update();
	}
}

void NavButton::setPlaying(bool playing) {
	if (_playing == playing) {
		return;
	}
	_playing = playing;
	if (_playing) {
		_equalizer.start();
	} else {
		_equalizer.stop();
	}
	update();
}

void NavButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	const auto over = isOver();
	if (_wasOver != over) {
		_wasOver = over;
		_over.start(
			[=] { update(); },
			over ? 0. : 1.,
			over ? 1. : 0.,
			kHoverDuration);
	}
}

QImage NavButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::hoursgramMusicNavRadius);
}

void NavButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto over = _over.value(isOver() ? 1. : 0.) * (1. - _active);
	if (over > 0.) {
		auto hq = PainterHighQualityEnabler(p);
		auto color = st::windowBgOver->c;
		color.setAlphaF(color.alphaF() * over);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(
			rect(),
			st::hoursgramMusicNavRadius,
			st::hoursgramMusicNavRadius);
	}
	paintRipple(p, 0, 0);

	const auto iconTop = (height() - _icon.height()) / 2;
	const auto iconLeft = st::hoursgramMusicNavIconLeft;
	if (_active < 1.) {
		p.setOpacity(1. - _active);
		_icon.paint(p, iconLeft, iconTop, width());
	}
	if (_active > 0.) {
		p.setOpacity(_active);
		_iconActive.paint(p, iconLeft, iconTop, width());
	}
	p.setOpacity(1.);

	const auto fg = anim::color(st::windowBoldFg, st::activeButtonFg, _active);
	const auto sub = anim::color(
		st::windowSubTextFg,
		st::activeButtonFg,
		_active);
	const auto left = st::hoursgramMusicNavTextLeft;
	auto right = st::hoursgramMusicNavIconLeft;
	if (_playing) {
		const auto size = st::hoursgramMusicRowEqHeight * 2;
		PaintEqualizer(
			p,
			QRect(width() - right - size, 0, size, height()),
			sub,
			true);
		right += size;
	} else if (!_count.isEmpty()) {
		p.setPen(sub);
		p.setFont(st::hoursgramMusicNavCountFont);
		const auto countWidth = st::hoursgramMusicNavCountFont->width(_count);
		p.drawText(
			width() - right - countWidth,
			(height() - st::hoursgramMusicNavCountFont->height) / 2
				+ st::hoursgramMusicNavCountFont->ascent,
			_count);
		right += countWidth + st::hoursgramMusicNavIconLeft;
	}
	p.setPen(fg);
	p.setFont(st::hoursgramMusicNavFont);
	const auto available = width() - left - right;
	p.drawText(
		left,
		(height() - st::hoursgramMusicNavFont->height) / 2
			+ st::hoursgramMusicNavFont->ascent,
		st::hoursgramMusicNavFont->elided(_text, std::max(available, 0)));
}

struct Choice {
	Section section = Section::Home;
	uint64 playlistId = 0;

	friend inline bool operator==(Choice, Choice) = default;
};

// The side menu: the main sections, then the playlists of the account.
// The highlight of the chosen item glides to the next one.
class SideMenu final : public Ui::RpWidget {
public:
	SideMenu(QWidget *parent, not_null<Main::Session*> session);

	void setChosen(Choice choice);
	[[nodiscard]] rpl::producer<Choice> chosen() const {
		return _chosen.events();
	}
	[[nodiscard]] rpl::producer<> createRequests() const {
		return _createRequests.events();
	}
	[[nodiscard]] rpl::producer<std::pair<uint64, QPoint>> menuRequests()
	const {
		return _menuRequests.events();
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Item {
		Choice choice;
		not_null<NavButton*> button;
	};

	void rebuildPlaylists();
	void refreshActive();
	void refreshPlaying();
	[[nodiscard]] NavButton *buttonFor(Choice choice) const;
	[[nodiscard]] NavButton *addItem(
		Choice choice,
		QString text,
		const style::icon &icon,
		const style::icon &iconActive);

	const not_null<Main::Session*> _session;
	std::vector<Item> _main;
	std::vector<Item> _playlists;
	object_ptr<Ui::IconButton> _add;
	Choice _choice;
	Ui::Animations::Simple _highlight;
	int _highlightFrom = -1;
	int _highlightTo = -1;
	int _sectionTop = 0;
	rpl::event_stream<Choice> _chosen;
	rpl::event_stream<> _createRequests;
	rpl::event_stream<std::pair<uint64, QPoint>> _menuRequests;

};

SideMenu::SideMenu(QWidget *parent, not_null<Main::Session*> session)
: RpWidget(parent)
, _session(session)
, _add(this, st::hoursgramMusicNavAdd) {
	addItem(
		{ Section::Home },
		Tr("Fresh wave", "Свежая волна"),
		st::hoursgramMusicNavHome,
		st::hoursgramMusicNavHomeActive);
	addItem(
		{ Section::Search },
		Tr("Search", "Поиск"),
		st::hoursgramMusicNavSearch,
		st::hoursgramMusicNavSearchActive);
	addItem(
		{ Section::Saved },
		Tr("My music", "Моя музыка"),
		st::hoursgramMusicNavSaved,
		st::hoursgramMusicNavSavedActive);
	addItem(
		{ Section::NowPlaying },
		Tr("Now playing", "Сейчас играет"),
		st::hoursgramMusicNavNow,
		st::hoursgramMusicNavNowActive);
	_main = base::take(_playlists);

	_add->setClickedCallback([=] {
		_createRequests.fire({});
	});
	_add->show();

	rebuildPlaylists();
	Playlists::Instance().changes() | rpl::on_next([=] {
		rebuildPlaylists();
	}, lifetime());

	rpl::merge(
		Player()->trackChanged() | rpl::to_empty,
		Player()->updatedNotifier() | rpl::to_empty
	) | rpl::on_next([=] {
		refreshPlaying();
	}, lifetime());
}

NavButton *SideMenu::addItem(
		Choice choice,
		QString text,
		const style::icon &icon,
		const style::icon &iconActive) {
	const auto button = Ui::CreateChild<NavButton>(
		this,
		std::move(text),
		icon,
		iconActive);
	button->setAcceptBoth(true);
	button->clicks() | rpl::on_next([=](Qt::MouseButton which) {
		if (which == Qt::RightButton) {
			if (choice.section == Section::Playlist) {
				_menuRequests.fire({ choice.playlistId, QCursor::pos() });
			}
			return;
		}
		_chosen.fire_copy(choice);
	}, button->lifetime());
	button->show();
	_playlists.push_back({ choice, button });
	return button;
}

void SideMenu::rebuildPlaylists() {
	for (const auto &item : base::take(_playlists)) {
		delete item.button.get();
	}
	const auto account = AccountOf(_session);
	for (const auto &playlist : Playlists::Instance().list(account)) {
		const auto button = addItem(
			{ Section::Playlist, playlist.id },
			playlist.title,
			st::hoursgramMusicNavPlaylist,
			st::hoursgramMusicNavPlaylistActive);
		button->setCount(int(playlist.tracks.size()));
	}
	resizeToWidth(width());
	_highlightFrom = _highlightTo = -1;
	refreshActive();
	update();
}

NavButton *SideMenu::buttonFor(Choice choice) const {
	for (const auto &list : { &_main, &_playlists }) {
		for (const auto &item : *list) {
			if (item.choice == choice) {
				return item.button;
			}
		}
	}
	return nullptr;
}

void SideMenu::setChosen(Choice choice) {
	if (choice.section != Section::Playlist) {
		choice.playlistId = 0;
	}
	if (_choice == choice) {
		return;
	}
	const auto from = buttonFor(_choice);
	const auto to = buttonFor(choice);
	_choice = choice;
	_highlightFrom = from ? from->y() : (to ? to->y() : -1);
	_highlightTo = to ? to->y() : -1;
	_highlight.stop();
	if (from && to) {
		_highlight.start(
			[=] { refreshActive(); update(); },
			0.,
			1.,
			kSelectDuration,
			anim::easeOutCubic);
	}
	refreshActive();
	update();
}

void SideMenu::refreshActive() {
	const auto to = buttonFor(_choice);
	if (to) {
		_highlightTo = to->y();
	}
	const auto progress = _highlight.value(1.);
	const auto top = (_highlightFrom < 0 || _highlightTo < 0)
		? _highlightTo
		: anim::interpolate(_highlightFrom, _highlightTo, progress);
	for (const auto &list : { &_main, &_playlists }) {
		for (const auto &item : *list) {
			const auto distance = std::abs(item.button->y() - top);
			const auto height = item.button->height();
			const auto active = (top < 0)
				? 0.
				: std::clamp(1. - float64(distance) / height, 0., 1.);
			item.button->setActive(active);
		}
	}
}

void SideMenu::refreshPlaying() {
	const auto audio = Player()->current(kType);
	const auto state = Player()->getState(kType);
	const auto playing = audio.audio()
		&& (state.id == audio)
		&& Media::Player::ShowPauseIcon(state.state);
	if (const auto button = buttonFor({ Section::NowPlaying })) {
		button->setPlaying(playing);
	}
}

int SideMenu::resizeGetHeight(int newWidth) {
	const auto padding = st::hoursgramMusicNavPadding;
	const auto inner = newWidth - padding.left() - padding.right();
	auto top = padding.top();
	for (const auto &item : _main) {
		item.button->setGeometry(
			padding.left(),
			top,
			inner,
			st::hoursgramMusicNavHeight);
		top += st::hoursgramMusicNavHeight;
	}
	top += st::hoursgramMusicGap;
	_sectionTop = top;
	_add->moveToRight(
		padding.right(),
		top + (st::hoursgramMusicNavSectionHeight - _add->height()) / 2,
		newWidth);
	top += st::hoursgramMusicNavSectionHeight;
	for (const auto &item : _playlists) {
		item.button->setGeometry(
			padding.left(),
			top,
			inner,
			st::hoursgramMusicNavHeight);
		top += st::hoursgramMusicNavHeight;
	}
	if (_playlists.empty()) {
		top += st::hoursgramMusicNavHeight;
	}
	refreshActive();
	return top + padding.bottom();
}

void SideMenu::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto padding = st::hoursgramMusicNavPadding;

	p.setPen(st::windowSubTextFg);
	p.setFont(st::hoursgramMusicNavSection);
	p.drawText(
		padding.left() + st::hoursgramMusicNavSectionLeft,
		_sectionTop
			+ (st::hoursgramMusicNavSectionHeight
				- st::hoursgramMusicNavSection->height) / 2
			+ st::hoursgramMusicNavSection->ascent,
		Tr("PLAYLISTS", "ПЛЕЙЛИСТЫ"));
	if (_playlists.empty()) {
		p.setFont(st::hoursgramMusicNavCountFont);
		p.drawText(
			padding.left() + st::hoursgramMusicNavSectionLeft,
			_sectionTop
				+ st::hoursgramMusicNavSectionHeight
				+ st::hoursgramMusicNavCountFont->ascent,
			Tr("Press + to create one", "Нажмите +, чтобы создать"));
	}

	const auto progress = _highlight.value(1.);
	const auto top = (_highlightFrom < 0 || _highlightTo < 0)
		? _highlightTo
		: anim::interpolate(_highlightFrom, _highlightTo, progress);
	if (top < 0) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgActive);
	p.drawRoundedRect(
		QRect(
			padding.left(),
			top,
			width() - padding.left() - padding.right(),
			st::hoursgramMusicNavHeight),
		st::hoursgramMusicNavRadius,
		st::hoursgramMusicNavRadius);
}

// The bar at the bottom of the window: the track, the transport and the
// seek bar, the same player as in the chats. The play button pops a
// little whenever playback starts or stops.
class PlayerBar final : public Ui::RpWidget {
public:
	PlayerBar(QWidget *parent);

	[[nodiscard]] rpl::producer<> openRequests() const {
		return _openRequests.events();
	}
	[[nodiscard]] rpl::producer<> addRequests() const {
		return _addRequests.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateTrack();
	void updateState(const Media::Player::TrackState &track);
	void layout();

	Artwork _artwork;
	AudioMsgId _audio;
	QString _title;
	QString _performer;
	QString _position;
	QString _duration;
	object_ptr<Ui::AbstractButton> _info;
	object_ptr<Ui::IconButton> _previous;
	object_ptr<Ui::IconButton> _play;
	object_ptr<Ui::IconButton> _next;
	object_ptr<Ui::MediaSlider> _slider;
	object_ptr<Ui::RoundButton> _lyrics;
	object_ptr<Ui::IconButton> _add;
	Ui::Animations::Simple _pop;
	crl::time _durationMs = 0;
	crl::time _seekPositionMs = -1;
	bool _playing = false;

};

PlayerBar::PlayerBar(QWidget *parent)
: RpWidget(parent)
, _artwork([=] { update(); })
, _info(this)
, _previous(this, st::hoursgramMusicBarPrevious)
, _play(this, st::hoursgramMusicBarPlay)
, _next(this, st::hoursgramMusicBarSide)
, _slider(this, st::hoursgramPlayerSlider)
, _lyrics(
	this,
	rpl::single(Tr("Lyrics", "Текст")),
	st::hoursgramMusicBarAction)
, _add(this, st::hoursgramMusicNavAdd) {
	resize(width(), st::hoursgramMusicBarHeight);
	_slider->resize(
		st::hoursgramMusicBarSliderWidth,
		st::hoursgramPlayerSliderHeight);
	for (const auto widget : std::initializer_list<QWidget*>{
			_info.data(),
			_previous.data(),
			_play.data(),
			_next.data(),
			_slider.data(),
			_lyrics.data(),
			_add.data() }) {
		widget->show();
	}

	_info->setClickedCallback([=] {
		_openRequests.fire({});
	});
	_lyrics->setClickedCallback([=] {
		_openRequests.fire({});
	});
	_add->setClickedCallback([=] {
		_addRequests.fire({});
	});
	_play->setClickedCallback([=] {
		Player()->playPause(kType);
	});
	_previous->setClickedCallback([=] {
		Player()->previous(kType);
	});
	_next->setClickedCallback([=] {
		Player()->next(kType);
	});

	_slider->setChangeProgressCallback([=](float64 value) {
		if (!_durationMs) {
			return;
		}
		_seekPositionMs = crl::time(value * _durationMs);
		_position = Ui::FormatDurationText(_seekPositionMs / 1000);
		Player()->startSeeking(kType);
		update();
	});
	_slider->setChangeFinishedCallback([=](float64 value) {
		_seekPositionMs = -1;
		if (_durationMs) {
			Player()->finishSeeking(kType, value);
		} else {
			Player()->cancelSeeking(kType);
		}
	});

	Player()->updatedNotifier(
	) | rpl::filter([](const Media::Player::TrackState &track) {
		return (track.id.type() == kType);
	}) | rpl::on_next([=](const Media::Player::TrackState &track) {
		updateTrack();
		updateState(track);
	}, lifetime());

	Player()->trackChanged(
	) | rpl::filter([](AudioMsgId::Type type) {
		return (type == kType);
	}) | rpl::on_next([=] {
		updateTrack();
	}, lifetime());

	updateTrack();
	updateState(Player()->getState(kType));
}

void PlayerBar::updateTrack() {
	const auto audio = Player()->current(kType);
	if (audio == _audio && (audio.audio() || !_title.isEmpty())) {
		return;
	}
	_audio = audio;
	if (const auto document = audio.audio()) {
		_title = TrackTitle(document);
		_performer = TrackPerformer(document);
		_artwork.setTrack(FromAudio(audio));
	} else {
		_title = Tr("Nothing is playing", "Ничего не играет");
		_performer = Tr(
			"Pick a song from the wave or find one",
			"Выберите трек из волны или найдите свой");
		_artwork.clear();
	}
	_add->setVisible(audio.audio() != nullptr);
	update();
}

void PlayerBar::updateState(const Media::Player::TrackState &track) {
	const auto frequency = track.frequency
		? track.frequency
		: Media::Player::kDefaultFrequency;
	_durationMs = (track.length * 1000LL) / frequency;
	const auto positionMs = (track.position * 1000LL) / frequency;
	if (_seekPositionMs < 0) {
		_position = Ui::FormatDurationText(positionMs / 1000);
		_slider->setValue(_durationMs
			? (float64(positionMs) / _durationMs)
			: 0.);
	}
	_duration = Ui::FormatDurationText(_durationMs / 1000);
	_slider->setDisabled(!_durationMs);
	const auto playing = Media::Player::ShowPauseIcon(track.state);
	if (_playing != playing) {
		_playing = playing;
		_play->setIconOverride(
			playing ? &st::hoursgramMusicBarPauseIcon : nullptr,
			playing ? &st::hoursgramMusicBarPauseIcon : nullptr);
		_pop.stop();
		_pop.start([=] { update(); }, 0., 1., kPopDuration);
	}
	_previous->setDisabled(!_audio);
	_next->setDisabled(!_audio);
	update();
}

void PlayerBar::resizeEvent(QResizeEvent *e) {
	layout();
}

void PlayerBar::layout() {
	const auto padding = st::hoursgramMusicBarPadding;
	const auto infoWidth = std::min(
		st::hoursgramMusicBarInfoWidth,
		width() / 3);
	_info->setGeometry(padding.left(), 0, infoWidth, height());

	const auto actionsWidth = st::hoursgramMusicBarActionsWidth;
	auto right = width() - padding.right();
	_add->moveToLeft(
		right - _add->width(),
		(height() - _add->height()) / 2,
		width());
	right -= _add->width() + st::hoursgramMusicActionSkip;
	_lyrics->moveToLeft(
		right - _lyrics->width(),
		(height() - _lyrics->height()) / 2,
		width());

	const auto middleLeft = padding.left() + infoWidth;
	const auto middleRight = width() - padding.right() - actionsWidth;
	const auto center = (middleLeft + middleRight) / 2;
	const auto controlsTop = st::hoursgramMusicGap / 2;
	const auto space = st::hoursgramMusicActionSkip;
	_play->moveToLeft(center - _play->width() / 2, controlsTop, width());
	_previous->moveToLeft(
		_play->x() - space - _previous->width(),
		controlsTop + (_play->height() - _previous->height()) / 2,
		width());
	_next->moveToLeft(
		_play->x() + _play->width() + space,
		controlsTop + (_play->height() - _next->height()) / 2,
		width());

	const auto timeWidth = st::hoursgramMusicBarTimeWidth;
	const auto sliderWidth = std::max(
		std::min(
			st::hoursgramMusicBarSliderWidth,
			middleRight - middleLeft - 2 * timeWidth),
		timeWidth);
	_slider->setGeometry(
		center - sliderWidth / 2,
		st::hoursgramMusicBarSliderTop,
		sliderWidth,
		st::hoursgramPlayerSliderHeight);
}

void PlayerBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(CardColor());
	p.drawRoundedRect(
		rect(),
		st::hoursgramMusicCardRadius,
		st::hoursgramMusicCardRadius);

	const auto padding = st::hoursgramMusicBarPadding;
	const auto size = st::hoursgramMusicBarCover;
	const auto cover = QRect(
		padding.left(),
		(height() - size) / 2,
		size,
		size);
	_artwork.paint(
		p,
		cover,
		st::hoursgramMusicBarCoverRadius,
		st::hoursgramMusicRowCoverIcon,
		st::windowBgRipple->c);

	const auto left = cover.x() + size + st::hoursgramMusicRowCoverSkip;
	const auto available = _info->x() + _info->width() - left;
	if (available > 0) {
		p.setPen(st::windowBoldFg);
		p.setFont(st::hoursgramMusicBarTitle);
		p.drawText(
			left,
			height() / 2 - st::hoursgramMusicGap / 4,
			st::hoursgramMusicBarTitle->elided(_title, available));
		p.setPen(st::windowSubTextFg);
		p.setFont(st::hoursgramMusicBarSub);
		p.drawText(
			left,
			height() / 2
				+ st::hoursgramMusicBarSub->height
				- st::hoursgramMusicGap / 4,
			st::hoursgramMusicBarSub->elided(_performer, available));
	}

	const auto pop = _pop.animating()
		? std::sin(kPi * _pop.value(1.)) * 0.12
		: 0.;
	const auto play = QRectF(_play->geometry());
	const auto center = play.center();
	const auto radius = (play.width() / 2.) * (1. + pop);
	p.setPen(Qt::NoPen);
	p.setBrush(_audio ? st::windowBgActive->c : st::windowBgRipple->c);
	p.drawEllipse(center, radius, radius);

	if (_audio) {
		const auto timeWidth = st::hoursgramMusicBarTimeWidth;
		const auto slider = _slider->geometry();
		const auto top = slider.y()
			+ (slider.height() - st::hoursgramMusicRowSub->height) / 2;
		p.setPen(st::windowSubTextFg);
		p.setFont(st::hoursgramMusicRowSub);
		p.drawText(
			QRect(slider.x() - timeWidth, top, timeWidth, slider.height()),
			_position,
			style::al_topleft);
		p.drawText(
			QRect(slider.x() + slider.width(), top, timeWidth, slider.height()),
			_duration,
			style::al_topright);
	}
}

// The logo in the title of the window. It lets the mouse through, so that
// the window can still be dragged by it.
class Logo final : public Ui::RpWidget {
public:
	explicit Logo(QWidget *parent) : RpWidget(parent) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		resize(st::hoursgramMusicLogoWidth, st::hoursgramMusicLogoSize);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto size = st::hoursgramMusicLogoSize;
		auto gradient = QLinearGradient(0, 0, size, size);
		gradient.setColorAt(0., st::windowBgActive->c);
		gradient.setColorAt(
			1.,
			anim::color(st::windowBgActive, st::windowBgRipple, 0.45));
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRoundedRect(
			QRect(0, 0, size, size),
			st::hoursgramMusicLogoRadius,
			st::hoursgramMusicLogoRadius);
		const auto &icon = st::hoursgramMusicLogoIcon;
		icon.paint(
			p,
			(size - icon.width()) / 2,
			(size - icon.height()) / 2,
			width());

		const auto left = size + st::hoursgramMusicLogoSkip;
		const auto &title = st::hoursgramMusicLogoTitle;
		const auto &sub = st::hoursgramMusicLogoSub;
		const auto top = (height() - title->height - sub->height) / 2;
		p.setPen(st::windowBoldFg);
		p.setFont(title);
		p.drawText(left, top + title->ascent, u"HorusGram"_q);
		p.setPen(st::windowSubTextFg);
		p.setFont(sub);
		p.drawText(
			left,
			top + title->height + sub->ascent,
			Tr("Music", "Музыка"));
	}

};

// The search pill: a rounded field with the glass on the left.
class SearchPill final : public Ui::RpWidget {
public:
	explicit SearchPill(QWidget *parent);

	[[nodiscard]] not_null<Ui::InputField*> field() const {
		return _field.data();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<Ui::InputField> _field;
	Ui::Animations::Simple _focus;
	bool _focused = false;

};

SearchPill::SearchPill(QWidget *parent)
: RpWidget(parent)
, _field(
	this,
	st::hoursgramMusicSearch,
	rpl::single(Tr(
		"Search songs and artists in Telegram",
		"Найти трек или исполнителя в Telegram"))) {
	resize(width(), st::hoursgramMusicSearchHeight);
	_field->show();
	_field->focusedChanges() | rpl::on_next([=](bool focused) {
		_focused = focused;
		_focus.start(
			[=] { update(); },
			focused ? 0. : 1.,
			focused ? 1. : 0.,
			kHoverDuration);
	}, lifetime());
}

void SearchPill::resizeEvent(QResizeEvent *e) {
	_field->setGeometry(rect());
}

void SearchPill::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto focus = _focus.value(_focused ? 1. : 0.);
	const auto radius = height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(anim::color(st::windowBgOver, st::windowBgRipple, focus * 0.5));
	p.drawRoundedRect(rect(), radius, radius);
	if (focus > 0.) {
		auto pen = QPen(st::windowBgActive->c);
		pen.setWidthF(style::ConvertScaleExact(1.5));
		auto color = pen.color();
		color.setAlphaF(focus);
		pen.setColor(color);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto half = pen.widthF() / 2.;
		p.drawRoundedRect(
			QRectF(rect()).marginsRemoved({ half, half, half, half }),
			radius - half,
			radius - half);
	}
	const auto &icon = st::hoursgramMusicSearchIcon;
	icon.paint(
		p,
		st::hoursgramMusicSearchIconLeft,
		(height() - icon.height()) / 2,
		width());
}

// A page that scrolls: the content column keeps the page padding and is
// as wide as the scroll area.
struct ScrollPage {
	object_ptr<Ui::RpWidget> widget;
	not_null<Ui::VerticalLayout*> content;
	not_null<Ui::ScrollArea*> scroll;
};

[[nodiscard]] ScrollPage MakeScrollPage(QWidget *parent) {
	auto widget = object_ptr<Ui::RpWidget>(parent);
	const auto raw = widget.data();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		raw,
		st::defaultScrollArea);
	const auto content = scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(scroll)).data();
	raw->sizeValue() | rpl::on_next([=](QSize size) {
		scroll->setGeometry(QRect(QPoint(), size));
		content->resizeToWidth(size.width());
	}, raw->lifetime());
	scroll->show();
	return { std::move(widget), content, scroll };
}

[[nodiscard]] style::margins PageMargins() {
	const auto padding = st::hoursgramMusicPagePadding;
	return { padding.left(), 0, padding.right(), 0 };
}

void AddSkipTo(not_null<Ui::VerticalLayout*> layout, int height) {
	layout->add(object_ptr<Ui::FixedHeightWidget>(layout, height));
}

void AddEmptyText(
		not_null<Ui::VerticalLayout*> layout,
		const QString &text) {
	layout->add(
		object_ptr<Ui::FlatLabel>(
			layout,
			rpl::single(text),
			st::hoursgramMusicEmpty),
		st::hoursgramMusicEmptyPadding);
}

class MusicWindow;

// What every page needs from the window around it.
struct Context {
	not_null<Window::SessionController*> controller;
	not_null<Main::Session*> session;
	std::shared_ptr<Ui::Show> show;
	Fn<void(Section, uint64)> open;
	Fn<void(QueueTrack, uint64)> trackMenu;
};

// Fills the layout with rows for the tracks; a click plays the list from
// that row on. The rows follow the player and the queue by themselves.
void FillTracks(
		not_null<Ui::VerticalLayout*> layout,
		const Context &context,
		std::vector<QueueTrack> tracks,
		uint64 playlistId,
		bool numbered) {
	auto rows = std::make_shared<std::vector<not_null<TrackRow*>>>();
	const auto shared = std::make_shared<std::vector<QueueTrack>>(
		std::move(tracks));
	const auto session = context.session;
	const auto menu = context.trackMenu;
	auto index = 0;
	for (const auto &track : *shared) {
		const auto row = layout->add(
			object_ptr<TrackRow>(layout, track, numbered ? (index + 1) : 0),
			PageMargins());
		row->setClickedCallback([=] {
			ActivateTrack(session, *shared, index);
		});
		if (menu) {
			row->setMenuCallback([=] {
				menu(row->track(), playlistId);
			});
		}
		row->appear(std::min(index * kAppearStagger, kAppearMaxDelay));
		rows->push_back(row);
		++index;
	}
	const auto refresh = [=] {
		for (const auto &row : *rows) {
			row->refreshState();
		}
	};
	rpl::merge(
		Player()->trackChanged() | rpl::to_empty,
		Player()->updatedNotifier() | rpl::to_empty,
		Queue::Instance().changes()
	) | rpl::on_next(refresh, layout->lifetime());
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateHomePage(
		QWidget *parent,
		const Context &context) {
	auto page = MakeScrollPage(parent);
	const auto content = page.content;
	const auto session = context.session;
	AddSkipTo(content, st::hoursgramMusicPagePadding.top());

	const auto body = content->add(object_ptr<Ui::VerticalLayout>(content));
	const auto loading = body->add(
		object_ptr<Skeleton>(body, st::hoursgramMusicSkeleton),
		PageMargins());
	const auto fill = [=](FreshWave wave) {
		body->clear();
		if (wave.failed) {
			AddEmptyText(body, Tr(
				"Could not load the charts. "
				"Check the connection and open the page again.",
				"Не удалось загрузить чарты. "
				"Проверьте подключение и откройте страницу ещё раз."));
			return;
		}
		auto fresh = std::vector<QueueTrack>();
		for (const auto &track : wave.fresh) {
			if (int(fresh.size()) >= kMaxFreshCards) {
				break;
			}
			fresh.push_back(FromChart(track));
		}
		const auto freshShared = std::make_shared<std::vector<QueueTrack>>(
			fresh);
		const auto hero = body->add(
			object_ptr<HeroBanner>(body, wave.fresh),
			PageMargins());
		hero->playRequests() | rpl::on_next([=] {
			ActivateTrack(session, *freshShared, 0);
		}, hero->lifetime());

		AddSkipTo(body, st::hoursgramMusicSectionSkip);
		body->add(
			object_ptr<PageHeader>(
				body,
				Tr("New releases", "Новые релизы"),
				Tr(
					"Out in the last weeks, newest first",
					"Вышли за последние недели, сначала самые новые"),
				false),
			PageMargins());
		AddSkipTo(body, st::hoursgramMusicHeaderButtonsSkip);
		const auto grid = body->add(
			object_ptr<CardsGrid>(body),
			PageMargins());
		auto index = 0;
		for (const auto &track : fresh) {
			const auto card = Ui::CreateChild<ChartCard>(grid, track);
			card->setClickedCallback([=] {
				ActivateTrack(session, *freshShared, index);
			});
			card->appear(std::min(index * kAppearStagger, kAppearMaxDelay));
			card->show();
			grid->add(card);
			++index;
		}
		grid->resizeToWidth(grid->width());
		rpl::merge(
			Player()->trackChanged() | rpl::to_empty,
			Player()->updatedNotifier() | rpl::to_empty,
			Queue::Instance().changes()
		) | rpl::on_next([=] {
			for (const auto &card : grid->cards()) {
				card->refreshState();
			}
		}, grid->lifetime());

		AddSkipTo(body, st::hoursgramMusicSectionSkip);
		body->add(
			object_ptr<PageHeader>(
				body,
				Tr("Top songs", "Чарт"),
				Tr(
					"What is played the most right now",
					"Самое популярное прямо сейчас"),
				false),
			PageMargins());
		AddSkipTo(body, st::hoursgramMusicHeaderButtonsSkip);
		auto chart = std::vector<QueueTrack>();
		chart.reserve(wave.chart.size());
		for (const auto &track : wave.chart) {
			chart.push_back(FromChart(track));
		}
		FillTracks(body, context, std::move(chart), 0, true);
		AddSkipTo(body, st::hoursgramMusicPagePadding.bottom());
	};
	(void)loading;
	LoadFreshWave(crl::guard(body, fill));
	return std::move(page.widget);
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateListPage(
		QWidget *parent,
		const Context &context,
		QString title,
		QString about,
		Fn<void(Fn<void(std::vector<AudioMsgId>)>)> load,
		QString empty) {
	auto page = MakeScrollPage(parent);
	const auto content = page.content;
	AddSkipTo(content, st::hoursgramMusicPagePadding.top());
	const auto header = content->add(
		object_ptr<PageHeader>(content, title, about, true),
		PageMargins());
	AddSkipTo(content, st::hoursgramMusicSectionSkip);
	const auto body = content->add(object_ptr<Ui::VerticalLayout>(content));
	body->add(
		object_ptr<Skeleton>(body, st::hoursgramMusicSkeleton),
		PageMargins());
	if (!load) {
		body->clear();
		AddEmptyText(body, empty);
		return std::move(page.widget);
	}
	load(crl::guard(body, [=](std::vector<AudioMsgId> found) {
		body->clear();
		if (found.empty()) {
			AddEmptyText(body, empty);
			return;
		}
		header->setTexts(
			title,
			about.isEmpty()
				? tr::hoursgram_PlaylistTracks(
					tr::now,
					lt_count,
					int(found.size()))
				: about);
		auto tracks = std::vector<QueueTrack>();
		tracks.reserve(found.size());
		for (const auto &audio : found) {
			tracks.push_back(FromAudio(audio));
		}
		FillTracks(body, context, std::move(tracks), 0, true);
		AddSkipTo(body, st::hoursgramMusicPagePadding.bottom());
	}));
	return std::move(page.widget);
}

// A playlist only remembers which message a track came from. When that
// message is not loaded yet, it is asked for, so the playlist plays after
// a restart too, without opening the chat it was added from.
[[nodiscard]] AudioMsgId ResolvePlaylistTrack(
		not_null<Main::Session*> session,
		const PlaylistTrack &track) {
	const auto item = session->data().message(track.peerId, track.msgId);
	const auto media = item ? item->media() : nullptr;
	const auto document = media ? media->document() : nullptr;
	if (document && document->id == track.documentId) {
		return AudioMsgId(document, item->fullId());
	}
	return AudioMsgId();
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreatePlaylistPage(
		QWidget *parent,
		const Context &context,
		uint64 playlistId) {
	auto page = MakeScrollPage(parent);
	const auto content = page.content;
	const auto session = context.session;
	const auto account = AccountOf(session);
	const auto show = context.show;
	AddSkipTo(content, st::hoursgramMusicPagePadding.top());
	const auto header = content->add(
		object_ptr<PageHeader>(content, QString(), QString(), true),
		PageMargins());
	AddSkipTo(content, st::hoursgramMusicHeaderButtonsSkip);

	const auto buttons = content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::hoursgramMusicActionHeight),
		PageMargins());
	const auto play = Ui::CreateChild<Ui::RoundButton>(
		buttons,
		rpl::single(Tr("Play", "Слушать")),
		st::hoursgramMusicAction);
	const auto shuffle = Ui::CreateChild<Ui::RoundButton>(
		buttons,
		rpl::single(Tr("Shuffle", "Перемешать")),
		st::hoursgramMusicActionSecondary);
	const auto rename = Ui::CreateChild<Ui::RoundButton>(
		buttons,
		rpl::single(Tr("Rename", "Переименовать")),
		st::hoursgramMusicActionSecondary);
	const auto remove = Ui::CreateChild<Ui::RoundButton>(
		buttons,
		rpl::single(Tr("Delete", "Удалить")),
		st::hoursgramMusicActionSecondary);
	const auto all = std::vector<Ui::RoundButton*>{
		play,
		shuffle,
		rename,
		remove,
	};
	for (const auto button : all) {
		button->show();
	}
	rpl::combine(
		play->widthValue(),
		shuffle->widthValue(),
		rename->widthValue(),
		remove->widthValue()
	) | rpl::on_next([=](int, int, int, int) {
		auto left = 0;
		for (const auto button : all) {
			button->moveToLeft(left, 0, buttons->width());
			left += button->width() + st::hoursgramMusicActionSkip;
		}
	}, buttons->lifetime());

	AddSkipTo(content, st::hoursgramMusicSectionSkip);
	const auto body = content->add(object_ptr<Ui::VerticalLayout>(content));

	struct State {
		std::vector<QueueTrack> tracks;
		base::flat_set<FullMsgId> requested;
		Fn<void()> rebuild;
		int missing = 0;
	};
	const auto state = body->lifetime().make_state<State>();

	state->rebuild = [=] {
		const auto playlist = Playlists::Instance().playlist(
			account,
			playlistId);
		if (!playlist) {
			context.open(Section::Home, 0);
			return;
		}
		header->setTexts(
			playlist->title,
			playlist->tracks.empty()
				? tr::hoursgram_PlaylistNoTracks(tr::now)
				: tr::hoursgram_PlaylistTracks(
					tr::now,
					lt_count,
					int(playlist->tracks.size())));
		body->clear();
		state->tracks.clear();
		state->missing = 0;
		for (const auto &track : playlist->tracks) {
			const auto audio = ResolvePlaylistTrack(session, track);
			if (audio) {
				state->tracks.push_back(FromAudio(audio));
				continue;
			}
			++state->missing;
			const auto id = FullMsgId(track.peerId, track.msgId);
			if (state->requested.emplace(id).second) {
				const auto peer = session->data().peer(track.peerId);
				session->api().requestMessageData(
					peer,
					track.msgId,
					crl::guard(body, [=] { state->rebuild(); }));
			}
		}
		if (state->tracks.empty()) {
			AddEmptyText(body, state->missing
				? Tr(
					"Loading the tracks of this playlist...",
					"Загружаем треки плейлиста...")
				: Tr(
					"This playlist is empty. Add songs to it with the "
					"\xE2\x8B\xAF button of any track.",
					"Плейлист пока пуст. Добавляйте треки кнопкой "
					"\xE2\x8B\xAF у любого трека."));
		} else {
			FillTracks(body, context, state->tracks, playlistId, true);
		}
		AddSkipTo(body, st::hoursgramMusicPagePadding.bottom());
	};
	state->rebuild();

	Playlists::Instance().changes() | rpl::on_next([=] {
		state->rebuild();
	}, body->lifetime());

	play->setClickedCallback([=] {
		ActivateTrack(session, state->tracks, 0);
	});
	shuffle->setClickedCallback([=] {
		if (state->tracks.empty()) {
			return;
		}
		auto tracks = state->tracks;
		for (auto i = int(tracks.size()) - 1; i > 0; --i) {
			std::swap(tracks[i], tracks[base::RandomIndex(i + 1)]);
		}
		Queue::Instance().start(session, std::move(tracks), 0);
	});
	rename->setClickedCallback([=] {
		const auto playlist = Playlists::Instance().playlist(
			account,
			playlistId);
		if (!playlist) {
			return;
		}
		PromptPlaylistName(
			show,
			tr::hoursgram_PlaylistRename(),
			playlist->title,
			[=](QString name) {
				Playlists::Instance().rename(account, playlistId, name);
			});
	});
	remove->setClickedCallback([=] {
		ConfirmDeletePlaylist(show, session, playlistId);
	});
	return std::move(page.widget);
}

// The page of the track in play: a big cover that breathes with the
// playback on the left, the lyrics filling the rest.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateNowPlayingPage(
		QWidget *parent,
		const Context &context) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto session = context.session;
	const auto show = context.show;

	struct State {
		explicit State(Fn<void()> repaint) : artwork(std::move(repaint)) {
		}

		Artwork artwork;
		Ui::Animations::Simple breathe;
		DocumentId lyricsFor = 0;
		uint64 lyricsGeneration = 0;
		crl::time durationMs = 0;
		bool playing = false;
	};
	const auto cover = Ui::CreateChild<Ui::RpWidget>(raw);
	const auto state = raw->lifetime().make_state<State>([=] {
		cover->update();
	});
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		st::hoursgramMusicNowTitle);
	const auto performer = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		st::hoursgramMusicNowPerformer);
	const auto add = Ui::CreateChild<Ui::RoundButton>(
		raw,
		rpl::single(Tr("Add to playlist", "В плейлист")),
		st::hoursgramMusicAction);
	const auto attach = Ui::CreateChild<Ui::RoundButton>(
		raw,
		rpl::single(Tr("Attach lyrics", "Свой текст")),
		st::hoursgramMusicActionSecondary);
	const auto lyrics = Ui::CreateChild<LyricsView>(raw);
	for (const auto widget : std::initializer_list<QWidget*>{
			cover,
			title,
			performer,
			add,
			attach,
			lyrics }) {
		widget->show();
	}

	cover->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(cover);
		auto hq = PainterHighQualityEnabler(p);
		const auto scale = 0.92 + 0.08 * state->breathe.value(
			state->playing ? 1. : 0.);
		const auto full = cover->width();
		const auto size = int(base::SafeRound(full * scale));
		const auto rect = QRect(
			(full - size) / 2,
			(full - size) / 2,
			size,
			size);
		auto glow = st::windowBgActive->c;
		glow.setAlphaF(0.16 * scale);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		const auto spread = st::hoursgramMusicGap;
		p.drawRoundedRect(
			rect.translated(0, spread),
			st::hoursgramMusicNowCoverRadius,
			st::hoursgramMusicNowCoverRadius);
		state->artwork.paint(
			p,
			rect,
			st::hoursgramMusicNowCoverRadius,
			st::hoursgramPlayerCoverIcon,
			st::windowBgOver->c);
	}, cover->lifetime());

	raw->sizeValue() | rpl::on_next([=](QSize size) {
		const auto padding = st::hoursgramMusicPagePadding;
		const auto leftWidth = std::min(
			st::hoursgramMusicNowLeftWidth,
			size.width() / 2);
		const auto coverSize = std::min(
			st::hoursgramMusicNowCover,
			leftWidth - padding.left());
		auto top = padding.top();
		cover->setGeometry(padding.left(), top, coverSize, coverSize);
		top += coverSize + st::hoursgramMusicSectionSkip;
		title->resizeToWidth(coverSize);
		title->moveToLeft(padding.left(), top);
		top += title->height() + st::hoursgramPlayerPerformerSkip;
		performer->resizeToWidth(coverSize);
		performer->moveToLeft(padding.left(), top);
		top += performer->height() + st::hoursgramMusicSectionSkip;
		add->moveToLeft(padding.left(), top);
		attach->moveToLeft(
			padding.left() + add->width() + st::hoursgramMusicActionSkip,
			top);
		lyrics->setGeometry(
			padding.left() + leftWidth,
			0,
			std::max(size.width() - leftWidth - padding.left(), 1),
			size.height());
	}, raw->lifetime());

	lyrics->setSeekCallback([=](crl::time at) {
		if (state->durationMs > 0) {
			Player()->startSeeking(kType);
			Player()->finishSeeking(
				kType,
				std::clamp(float64(at) / state->durationMs, 0., 1.));
		}
	});

	const auto reloadLyrics = [=](DocumentData *document) {
		state->lyricsFor = document ? document->id : DocumentId();
		const auto generation = ++state->lyricsGeneration;
		auto loaded = document ? LoadLyrics(document) : Lyrics();
		const auto found = !loaded.empty();
		lyrics->setLyrics(std::move(loaded));
		if (!document) {
			lyrics->setPlaceholder(Tr(
				"Lyrics of the track that plays show up here.",
				"Здесь появится текст трека, который играет."));
			return;
		} else if (found) {
			return;
		}
		lyrics->setPlaceholder(Tr(
			"Looking for the lyrics...",
			"Ищем текст песни..."));
		const auto id = document->id;
		SearchLyricsOnline(document, crl::guard(raw, [=](Lyrics result) {
			if (state->lyricsFor == id) {
				lyrics->setLyrics(std::move(result));
			}
		}));
		base::call_delayed(kLyricsWaitOnline, raw, [=] {
			if (state->lyricsGeneration == generation) {
				lyrics->setPlaceholder(Tr(
					"No lyrics found for this track. "
					"You can attach an .lrc or .txt file.",
					"Текст для этого трека не нашёлся. "
					"Можно прикрепить свой файл .lrc или .txt."));
			}
		});
	};

	const auto updateTrack = [=] {
		const auto audio = Player()->current(kType);
		const auto document = audio.audio();
		add->setVisible(document != nullptr);
		attach->setVisible(document != nullptr);
		if (!document) {
			title->setText(Tr("Nothing is playing", "Ничего не играет"));
			performer->setText(Tr(
				"Pick a song in the fresh wave or find one in search.",
				"Выберите трек в свежей волне или найдите в поиске."));
			state->artwork.clear();
			if (state->lyricsFor) {
				reloadLyrics(nullptr);
			} else {
				lyrics->setPlaceholder(Tr(
					"Lyrics of the track that plays show up here.",
					"Здесь появится текст трека, который играет."));
			}
			return;
		}
		title->setText(TrackTitle(document));
		performer->setText(TrackPerformer(document));
		state->artwork.setTrack(FromAudio(audio));
		if (state->lyricsFor != document->id) {
			reloadLyrics(document);
		}
	};
	const auto updateState = [=](const Media::Player::TrackState &track) {
		const auto frequency = track.frequency
			? track.frequency
			: Media::Player::kDefaultFrequency;
		state->durationMs = (track.length * 1000LL) / frequency;
		lyrics->setPosition((track.position * 1000LL) / frequency);
		const auto playing = Media::Player::ShowPauseIcon(track.state);
		if (state->playing != playing) {
			state->playing = playing;
			state->breathe.start(
				[=] { cover->update(); },
				playing ? 0. : 1.,
				playing ? 1. : 0.,
				kCoverDuration,
				anim::easeOutCubic);
		}
	};

	add->setClickedCallback([=] {
		const auto audio = Player()->current(kType);
		if (audio.audio()) {
			ShowAddToPlaylist(show, session, audio);
		}
	});
	attach->setClickedCallback([=] {
		const auto document = Player()->current(kType).audio();
		if (!document) {
			return;
		}
		const auto id = document->id;
		FileDialog::GetOpenPath(
			raw,
			tr::hoursgram_LyricsAttach(tr::now),
			tr::hoursgram_LyricsFilter(tr::now),
			crl::guard(raw, [=](FileDialog::OpenResult &&result) {
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

	Player()->updatedNotifier(
	) | rpl::filter([](const Media::Player::TrackState &track) {
		return (track.id.type() == kType);
	}) | rpl::on_next([=](const Media::Player::TrackState &track) {
		updateTrack();
		updateState(track);
	}, raw->lifetime());
	Player()->trackChanged(
	) | rpl::filter([](AudioMsgId::Type type) {
		return (type == kType);
	}) | rpl::on_next([=] {
		updateTrack();
	}, raw->lifetime());

	updateTrack();
	updateState(Player()->getState(kType));
	return result;
}

class MusicWindow final : public base::has_weak_ptr {
public:
	explicit MusicWindow(not_null<Window::SessionController*> controller);

	[[nodiscard]] Window::SessionController *controller() const {
		return _controller;
	}
	void showSection(Section section, uint64 playlistId);
	void activate();

	[[nodiscard]] rpl::producer<> closed() const {
		return _closed.events();
	}

private:
	void setupHeader();
	void setupBody();
	void layoutHeader();
	void searchChanged();
	void showTrackMenu(QueueTrack track, uint64 playlistId);
	void showPlaylistMenu(uint64 playlistId, QPoint position);
	void createPlaylist();
	[[nodiscard]] Context context();
	[[nodiscard]] object_ptr<Ui::RpWidget> createPage(
		Section section,
		uint64 playlistId);

	const not_null<Window::SessionController*> _controller;
	const not_null<Main::Session*> _session;
	base::unique_qptr<Ui::SeparatePanel> _panel;
	std::shared_ptr<Ui::Show> _show;
	Logo *_logo = nullptr;
	SearchPill *_search = nullptr;
	Ui::RpWidget *_side = nullptr;
	SideMenu *_menu = nullptr;
	PageStack *_pages = nullptr;
	PlayerBar *_bar = nullptr;
	base::unique_qptr<Ui::PopupMenu> _popup;
	base::Timer _searchTimer;
	Section _section = Section::Home;
	uint64 _playlistId = 0;
	bool _shown = false;
	QString _query;
	rpl::event_stream<> _closed;

};

MusicWindow::MusicWindow(not_null<Window::SessionController*> controller)
: _controller(controller)
, _session(&controller->session())
, _panel(base::make_unique_q<Ui::SeparatePanel>())
, _searchTimer([=] { searchChanged(); }) {
	_panel->setTitle(rpl::single(QString()));
	_panel->setInnerSize(st::hoursgramMusicWindowSize, true);
	_panel->window()->setWindowTitle(Tr("HorusGram Music", "HorusGram Музыка"));
	_show = _panel->uiShow();

	_panel->closeRequests() | rpl::on_next([=] {
		_panel->hideGetDuration();
	}, _panel->lifetime());
	_panel->closeEvents() | rpl::on_next([=] {
		_closed.fire({});
	}, _panel->lifetime());

	setupHeader();
	setupBody();

	Queue::Instance().notFound() | rpl::on_next([=](const ChartTrack &track) {
		_show->showToast(Tr(
			"Could not find \xC2\xAB%1\xC2\xBB in Telegram, skipping.",
			"\xC2\xAB%1\xC2\xBB не нашёлся в Telegram, пропускаем.").arg(
				track.artist + u" — "_q + track.title));
	}, _panel->lifetime());
}

void MusicWindow::setupHeader() {
	_logo = Ui::CreateChild<Logo>(_panel.get());
	_search = Ui::CreateChild<SearchPill>(_panel.get());
	_logo->show();
	_search->show();

	_panel->sizeValue() | rpl::on_next([=](QSize) {
		layoutHeader();
	}, _logo->lifetime());

	const auto field = _search->field();
	field->changes() | rpl::on_next([=] {
		_searchTimer.callOnce(kSearchDelay);
	}, field->lifetime());
	field->submits() | rpl::on_next([=] {
		_searchTimer.cancel();
		searchChanged();
	}, field->lifetime());
	field->cancelled() | rpl::on_next([=] {
		field->clear();
	}, field->lifetime());
}

void MusicWindow::layoutHeader() {
	const auto inner = _panel->innerGeometry();
	if (inner.isEmpty()) {
		return;
	}
	const auto titleTop = inner.y() - st::separatePanelTitleHeight;
	_logo->moveToLeft(
		inner.x() + st::hoursgramMusicLogoLeft,
		titleTop + (st::separatePanelTitleHeight - _logo->height()) / 2);
	const auto left = inner.x() + st::hoursgramMusicSideWidth;
	const auto right = inner.x()
		+ inner.width()
		- st::hoursgramMusicSearchRight;
	_search->setGeometry(
		left,
		titleTop
			+ (st::separatePanelTitleHeight - st::hoursgramMusicSearchHeight)
				/ 2,
		std::max(right - left, st::hoursgramMusicCardWidth),
		st::hoursgramMusicSearchHeight);
	_logo->raise();
	_search->raise();
}

void MusicWindow::setupBody() {
	auto owned = base::make_unique_q<Ui::RpWidget>(_panel.get());
	const auto body = owned.get();

	_side = Ui::CreateChild<Ui::RpWidget>(body);
	const auto sideScroll = Ui::CreateChild<Ui::ScrollArea>(
		_side,
		st::defaultScrollArea);
	_menu = sideScroll->setOwnedWidget(
		object_ptr<SideMenu>(sideScroll, _session)).data();
	_pages = Ui::CreateChild<PageStack>(body);
	_bar = Ui::CreateChild<PlayerBar>(body);
	_side->show();
	sideScroll->show();
	_pages->show();
	_bar->show();

	_side->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_side);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(CardColor());
		p.drawRoundedRect(
			_side->rect(),
			st::hoursgramMusicCardRadius,
			st::hoursgramMusicCardRadius);
	}, _side->lifetime());

	body->sizeValue() | rpl::on_next([=](QSize size) {
		const auto gap = st::hoursgramMusicGap;
		const auto barHeight = st::hoursgramMusicBarHeight;
		const auto sideWidth = st::hoursgramMusicSideWidth - gap * 2;
		const auto top = 0;
		const auto bottom = size.height() - gap - barHeight;
		_side->setGeometry(gap, top, sideWidth, bottom - top - gap);
		const auto radius = st::hoursgramMusicCardRadius;
		sideScroll->setGeometry(
			0,
			radius / 2,
			_side->width(),
			_side->height() - radius);
		_menu->resizeToWidth(_side->width());
		_pages->setGeometry(
			gap * 2 + sideWidth,
			top,
			size.width() - sideWidth - gap * 3,
			bottom - top - gap);
		_bar->setGeometry(gap, bottom, size.width() - gap * 2, barHeight);
	}, body->lifetime());

	_menu->chosen() | rpl::on_next([=](Choice choice) {
		if (choice.section == Section::Search) {
			_search->field()->setFocusFast();
			if (_query.isEmpty()) {
				_menu->setChosen({ _section, _playlistId });
				return;
			}
		}
		showSection(choice.section, choice.playlistId);
	}, _menu->lifetime());
	_menu->createRequests() | rpl::on_next([=] {
		createPlaylist();
	}, _menu->lifetime());
	_menu->menuRequests(
	) | rpl::on_next([=](std::pair<uint64, QPoint> request) {
		showPlaylistMenu(request.first, request.second);
	}, _menu->lifetime());

	_bar->openRequests() | rpl::on_next([=] {
		showSection(Section::NowPlaying, 0);
	}, _bar->lifetime());
	_bar->addRequests() | rpl::on_next([=] {
		const auto audio = Player()->current(kType);
		if (audio.audio()) {
			ShowAddToPlaylist(_show, _session, audio);
		}
	}, _bar->lifetime());

	_panel->showInner(std::move(owned));
}

Context MusicWindow::context() {
	const auto weak = base::make_weak(this);
	return {
		.controller = _controller,
		.session = _session,
		.show = _show,
		.open = [=](Section section, uint64 playlistId) {
			crl::on_main([=] {
				if (const auto strong = weak.get()) {
					strong->showSection(section, playlistId);
				}
			});
		},
		.trackMenu = [=](QueueTrack track, uint64 playlistId) {
			if (const auto strong = weak.get()) {
				strong->showTrackMenu(std::move(track), playlistId);
			}
		},
	};
}

void MusicWindow::searchChanged() {
	const auto query = _search->field()->getLastText().trimmed();
	if (query == _query) {
		return;
	}
	_query = query;
	if (_query.isEmpty()) {
		if (_section == Section::Search) {
			showSection(Section::Home, 0);
		}
		return;
	}
	showSection(Section::Search, 0);
}

void MusicWindow::showSection(Section section, uint64 playlistId) {
	if (section != Section::Playlist) {
		playlistId = 0;
	}
	const auto same = _shown
		&& (_section == section)
		&& (_playlistId == playlistId);
	if (same && section != Section::Search) {
		return;
	}
	const auto direction = _shown
		? ((SectionOrder(section, playlistId)
			< SectionOrder(_section, _playlistId)) ? -1 : 1)
		: 0;
	_section = section;
	_playlistId = playlistId;
	_shown = true;
	_menu->setChosen({ section, playlistId });
	_pages->showPage(createPage(section, playlistId), direction);
}

object_ptr<Ui::RpWidget> MusicWindow::createPage(
		Section section,
		uint64 playlistId) {
	const auto context = this->context();
	const auto session = _session;
	switch (section) {
	case Section::Home:
		return CreateHomePage(_pages, context);
	case Section::Search: {
		const auto query = _query;
		return CreateListPage(
			_pages,
			context,
			Tr("Search", "Поиск"),
			Tr(
				"Songs posted in public Telegram chats",
				"Треки из публичных чатов и каналов Telegram"),
			query.isEmpty()
				? Fn<void(Fn<void(std::vector<AudioMsgId>)>)>()
				: [=](Fn<void(std::vector<AudioMsgId>)> done) {
					SearchMusic(session, query, std::move(done));
				},
			query.isEmpty()
				? Tr(
					"Type a song or an artist in the field above.",
					"Введите название трека или исполнителя в поле выше.")
				: Tr(
					"Nothing found. Try other words.",
					"Ничего не нашлось. Попробуйте другие слова."));
	}
	case Section::Saved:
		return CreateListPage(
			_pages,
			context,
			Tr("My music", "Моя музыка"),
			QString(),
			[=](Fn<void(std::vector<AudioMsgId>)> done) {
				LoadSavedMusic(session, std::move(done));
			},
			Tr(
				"Music from your Saved Messages shows up here. "
				"Forward any track there to keep it.",
				"Здесь будет музыка из «Избранного». "
				"Перешлите туда любой трек, чтобы он сохранился."));
	case Section::NowPlaying:
		return CreateNowPlayingPage(_pages, context);
	case Section::Playlist:
		return CreatePlaylistPage(_pages, context, playlistId);
	}
	return CreateHomePage(_pages, context);
}

void MusicWindow::createPlaylist() {
	const auto account = AccountOf(_session);
	const auto weak = base::make_weak(this);
	PromptPlaylistName(
		_show,
		tr::hoursgram_PlaylistNew(),
		QString(),
		[=](QString name) {
			const auto id = Playlists::Instance().create(account, name);
			if (const auto strong = weak.get(); strong && id) {
				strong->showSection(Section::Playlist, id);
			}
		});
}

void MusicWindow::showPlaylistMenu(uint64 playlistId, QPoint position) {
	const auto account = AccountOf(_session);
	_popup = base::make_unique_q<Ui::PopupMenu>(
		_panel.get(),
		st::popupMenuWithIcons);
	const auto show = _show;
	const auto session = _session;
	_popup->addAction(
		tr::hoursgram_PlaylistRename(tr::now),
		[=] {
			const auto playlist = Playlists::Instance().playlist(
				account,
				playlistId);
			if (!playlist) {
				return;
			}
			PromptPlaylistName(
				show,
				tr::hoursgram_PlaylistRename(),
				playlist->title,
				[=](QString name) {
					Playlists::Instance().rename(account, playlistId, name);
				});
		},
		&st::menuIconEdit);
	_popup->addAction(
		tr::hoursgram_PlaylistDelete(tr::now),
		[=] {
			ConfirmDeletePlaylist(show, session, playlistId);
		},
		&st::menuIconDelete);
	_popup->popup(position);
}

void MusicWindow::showTrackMenu(QueueTrack track, uint64 playlistId) {
	const auto session = _session;
	const auto show = _show;
	const auto controller = _controller;
	_popup = base::make_unique_q<Ui::PopupMenu>(
		_panel.get(),
		st::popupMenuWithIcons);
	const auto withAudio = [=](Fn<void(AudioMsgId)> callback) {
		if (track.resolved()) {
			callback(track.audio);
			return;
		}
		ResolveChartTrack(session, track.chart, [=](AudioMsgId audio) {
			if (audio) {
				callback(audio);
			} else {
				show->showToast(Tr(
					"This song is not found in Telegram yet.",
					"Этот трек пока не нашёлся в Telegram."));
			}
		});
	};
	_popup->addAction(
		tr::hoursgram_AddToPlaylist(tr::now),
		[=] {
			withAudio([=](AudioMsgId audio) {
				ShowAddToPlaylist(show, session, audio);
			});
		},
		&st::menuIconPlaylists);
	if (playlistId && track.resolved()) {
		const auto account = AccountOf(session);
		const auto documentId = track.audio.audio()->id;
		_popup->addAction(
			tr::hoursgram_PlaylistRemoveTrack(tr::now),
			[=] {
				Playlists::Instance().removeTrack(
					account,
					playlistId,
					documentId);
			},
			&st::menuIconDelete);
	}
	if (track.resolved()) {
		const auto id = track.audio.contextId();
		_popup->addAction(
			Tr("Show in chat", "Показать в чате"),
			[=] {
				controller->showPeerHistory(
					id.peer,
					Window::SectionShow::Way::Forward,
					id.msg);
				controller->window().activate();
			},
			&st::menuIconShowInChat);
	}
	_popup->popup(QCursor::pos());
}

void MusicWindow::activate() {
	_panel->showAndActivate();
	layoutHeader();
}

std::unique_ptr<MusicWindow> GlobalWindow;

} // namespace

void ShowMusic(
		not_null<Window::SessionController*> controller,
		Section section,
		uint64 playlistId) {
	if (GlobalWindow && GlobalWindow->controller() != controller) {
		GlobalWindow = nullptr;
	}
	if (!GlobalWindow) {
		GlobalWindow = std::make_unique<MusicWindow>(controller);
		const auto raw = GlobalWindow.get();
		GlobalWindow->closed() | rpl::on_next([=] {
			crl::on_main([=] {
				if (GlobalWindow.get() == raw) {
					GlobalWindow = nullptr;
				}
			});
		}, controller->lifetime());
		controller->lifetime().add([=] {
			if (GlobalWindow.get() == raw) {
				GlobalWindow = nullptr;
			}
		});
	}
	GlobalWindow->showSection(section, playlistId);
	GlobalWindow->activate();
}

void ShowPlayer(not_null<Window::SessionController*> controller) {
	ShowMusic(
		controller,
		Player()->current(kType).audio()
			? Section::NowPlaying
			: Section::Home);
}

void ShowPlayerWindow(
		not_null<Window::SessionController*> controller,
		const AudioMsgId &audioId) {
	if (audioId.audio() && !(Player()->current(kType) == audioId)) {
		Queue::Instance().forget();
		Player()->play(audioId);
	}
	ShowMusic(controller, Section::NowPlaying);
}

} // namespace HoursGram::Music
