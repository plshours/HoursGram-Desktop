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

	return st::windowBgOver->c;
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
	std::optional<QColor> overrideBg,
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
	  , _overrideBg(overrideBg) {
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
	const auto size = st::infoMusicButtonTitle.style.font->height;
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
					.options = Images::Option::RoundSmall,
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

void AyuMusicButton::paintEvent(QPaintEvent *e) {
	if (!_currentCover) {
		return;
	}

	auto p = Painter(this);

	const auto size = st::infoMusicButtonTitle.style.font->height;
	const auto cover = _currentCover.value();
	const auto radius = st::infoProfileMusicRadius;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(cover.bg);
	p.drawRoundedRect(rect(), radius, radius);
	paintRipple(p, QPoint());

	if (!cover.pix.isNull()) {
		auto hq = PainterHighQualityEnabler(p);
		p.drawPixmap(
			QRect(_coverLeft, st::infoMusicButtonPadding.top(), size, size),
			cover.pix);
	}
}

// Keeps the ripple inside the rounded shape of the button.
QImage AyuMusicButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::infoProfileMusicRadius);
}

int AyuMusicButton::resizeGetHeight(int newWidth) {
	const auto padding = st::infoMusicButtonPadding;
	const auto &font = st::infoMusicButtonTitle.style.font;
	const auto coverSize = font->height;
	const auto skip = st::infoMusicButtonSkip;

	_performer->hide();
	const auto available = newWidth
		- padding.left()
		- padding.right()
		- coverSize
		- skip;
	if (available <= 0) {
		return padding.top() + font->height + padding.bottom();
	}
	_title->resizeToNaturalWidth(available);
	const auto used = coverSize + skip + _title->width();
	_coverLeft = std::max((newWidth - used) / 2, padding.left());
	_title->moveToLeft(
		_coverLeft + coverSize + skip,
		padding.top(),
		newWidth);

	return padding.top() + font->height + padding.bottom();
}

} // namespace Info::Profile
