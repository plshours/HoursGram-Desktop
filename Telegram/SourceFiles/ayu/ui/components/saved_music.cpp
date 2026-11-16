// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/saved_music.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/utils/itunes_search.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "info/profile/info_profile_music_button.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "styles/palette.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/image/image.h"
#include "ui/widgets/labels.h"
#include "window/themes/window_theme.h"

#include <QSvgRenderer>

namespace Info::Profile {

namespace {

QColor GetNoCoverBgColor(std::optional<QColor> overrideBg) {
	if (overrideBg) {
		return Ui::BlendColors(
			*overrideBg,
			Qt::black,
			st::infoProfileTopBarActionButtonBgOpacity);
	}

	return st::windowBg->c;
}

struct Cover
{
	QImage image;
	bool noCover;
};

// Everything the placeholder needs from the theme is read on the main
// thread and handed in, because the theme is not safe to touch from the
// worker this runs on.
struct CoverTheme
{
	QColor glyphFg;
	QColor bg;
};

QImage MakeNoCoverImage(const QSize &size, const CoverTheme &theme) {
	auto image = QImage(size, QImage::Format_ARGB32);
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		image.fill(theme.bg);

		auto svgIcon = QSvgRenderer(u":/gui/icons/ayu/nocover.svg"_q);
		p.setPen(QPen(theme.glyphFg));
		svgIcon.render(&p, QRect(0, 0, size.width(), size.height()));
	}
	return image;
}

} // namespace

Cover GetCurrentCover(
	const std::shared_ptr<Data::DocumentMedia> &dataMedia,
	const QSize &size,
	const CoverTheme &theme) {
	const auto placeholder = [&] {
		return Cover{
			.image = MakeNoCoverImage(size, theme),
			.noCover = true
		};
	};
	if (!dataMedia) {
		return placeholder();
	}

	const auto scaled = [&](not_null<Image*> image)
	{
		const auto aspectRatio = Qt::KeepAspectRatioByExpanding;
		const auto targetSize = size * style::DevicePixelRatio();
		return image->size().scaled(targetSize, aspectRatio);
	};
	if (const auto normal = dataMedia->thumbnail()) {
		return {
			.image = normal->original().scaled(
				scaled(normal),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation),
			.noCover = false
		};
	}

	return placeholder();
}

AyuMusicButton::AyuMusicButton(
	QWidget *parent,
	MusicButtonData data,
	std::optional<QColor> /*overrideBg*/,
	Fn<void()> handler)
	: RippleButton(parent, st::infoMusicButtonRipple)
	  , _performer(std::make_unique<Ui::FlatLabel>(
		  this,
		  data.performer,
		  st::infoMusicButtonPerformer))
	  , _title(std::make_unique<Ui::FlatLabel>(
		  this,
		  data.title,
		  st::infoMusicButtonTitle))
	  , _mediaView(data.mediaView)
	  // The colour of the profile header is not used: on a dark header the
	  // card became part of it and stuck to the name above it.
	  , _overrideBg(std::nullopt) {
	_performerText = data.performer;
	_titleText = data.title;
	rpl::combine(
		_title->naturalWidthValue(),
		_performer->naturalWidthValue()
	) | rpl::on_next([=]
							 {
								 resizeToWidth(widthNoMargins());
							 },
							 lifetime());

	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_performer->setAttribute(Qt::WA_TransparentForMouseEvents);

	downloadAndMakeCover(data.msgId);

	setClickedCallback(std::move(handler));
}

AyuMusicButton::~AyuMusicButton() = default;

void AyuMusicButton::updateData(MusicButtonData data) {
	_performer->setText(data.performer);
	_title->setText(data.title);
	_performerText = data.performer;
	_titleText = data.title;
	_mediaView = data.mediaView;
	downloadAndMakeCover(data.msgId);

	resizeToWidth(widthNoMargins());
}

void AyuMusicButton::downloadAndMakeCover(FullMsgId msgId) {
	if (_mediaView && _mediaView->owner()->isSongWithCover() && !_mediaView->thumbnail()) {
		const auto settings = &_mediaView->owner()->session().settings().autoDownload();
		// Data::AutoDownload::Type::Music always returns false
		if (settings->shouldDownload(Data::AutoDownload::Source::User,
									 Data::AutoDownload::Type::File,
									 _mediaView->owner()->size)) {
			_mediaView->thumbnailWanted(Data::FileOrigin(msgId));
			_mediaView->owner()->owner().session().downloaderTaskFinished(
			) | rpl::take_while([=]
			{
				if (_mediaView->thumbnail()) {
					makeCover();
				}
				return !_mediaView->thumbnail();
			}) | rpl::start(lifetime());
			return;
		}
	}

	makeCover();
}

void AyuMusicButton::makeCover() {
	const auto weak = base::make_weak(this);
	const auto size = st::infoMusicButtonCover;
	const auto bgColor = GetNoCoverBgColor(_overrideBg);
	const auto theme = CoverTheme{
		.glyphFg = st::windowBoldFg->c,
		.bg = Window::Theme::IsNightMode()
			? st::windowBoldFg->c.darker()
			: st::windowBoldFg->c.lighter(),
	};
	crl::async(
		[=, mediaView = _mediaView, performerText = _performerText, titleText = _titleText, overrideBg = _overrideBg]()
		{
			auto cover = GetCurrentCover(mediaView, QSize(size, size), theme);

			if (cover.noCover) {
				auto fetched = Ayu::Ui::Itunes::FetchCover(
					performerText,
					titleText,
					size);
				if (!fetched.isNull()) {
					cover.image = std::move(fetched);
					cover.noCover = false;
				}
			}

			crl::on_main([weak, cover = std::move(cover), bgColor, overrideBg, size]() mutable
			{
				const auto strong = weak.get();
				if (!strong) {
					return;
				}

				const auto target = QSize(size, size);
				const auto args = Images::PrepareArgs{
					.options = Images::Option::RoundLarge,
					.outer = target,
				};
				strong->_currentCover = {
					.pix = cover.image.isNull()
						? QPixmap()
						: Image(std::move(cover.image)).pix(target, args),
					.bg = bgColor,
					.noCover = cover.noCover,
				};

				strong->_title->setTextColorOverride(
					overrideBg ? st::groupCallMembersFg->c : st::windowBoldFg->c);
				strong->_performer->setTextColorOverride(
					overrideBg ? st::groupCallMembersFg->c : st::windowSubTextFg->c);

				strong->repaint();
				strong->_title->repaint();
				strong->_performer->repaint();

				strong->_onReady.fire({});
			});
		});
}

// A card like the sections under it: the cover on the left, the title and
// the performer on two lines next to it and an arrow to the whole list.
void AyuMusicButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto radius = st::infoProfileMusicRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(_currentCover ? _currentCover->bg : st::windowBg->c);
	p.drawRoundedRect(rect(), radius, radius);
	paintRipple(p, QPoint());

	const auto size = st::infoMusicButtonCover;
	const auto cover = QRect(
		_coverLeft,
		(height() - size) / 2,
		size,
		size);
	if (_currentCover && !_currentCover->pix.isNull()) {
		p.drawPixmap(cover, _currentCover->pix);
	} else {
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			cover,
			st::infoMusicButtonCoverRadius,
			st::infoMusicButtonCoverRadius);
	}

	const auto &arrow = st::topicButtonArrow;
	arrow.paint(
		p,
		width() - st::infoMusicButtonPadding.right() - arrow.width(),
		(height() - arrow.height()) / 2,
		width(),
		st::windowSubTextFg->c);
}

// Keeps the ripple inside the rounded shape of the button.
QImage AyuMusicButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::infoProfileMusicRadius);
}

int AyuMusicButton::resizeGetHeight(int newWidth) {
	const auto padding = st::infoMusicButtonPadding;
	const auto coverSize = st::infoMusicButtonCover;
	const auto skip = st::infoMusicButtonSkip;
	const auto result = padding.top() + coverSize + padding.bottom();

	_coverLeft = padding.left();
	const auto textLeft = _coverLeft + coverSize + skip;
	const auto available = newWidth
		- textLeft
		- skip
		- st::topicButtonArrow.width()
		- padding.right();
	if (available <= 0) {
		_title->hide();
		_performer->hide();
		return result;
	}
	_title->show();
	_title->resizeToNaturalWidth(available);
	const auto withPerformer = !_performerText.isEmpty();
	_performer->setVisible(withPerformer);
	if (withPerformer) {
		_performer->resizeToNaturalWidth(available);
	}
	const auto lines = _title->height() + (withPerformer
		? (st::infoMusicButtonLinesSkip + _performer->height())
		: 0);
	const auto top = (result - lines) / 2;
	_title->moveToLeft(textLeft, top, newWidth);
	if (withPerformer) {
		_performer->moveToLeft(
			textLeft,
			top + _title->height() + st::infoMusicButtonLinesSkip,
			newWidth);
	}
	return result;
}

} // namespace Info::Profile
