// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/avatar_corners_preview.h"

#include "ayu/ayu_settings.h"
#include "lang_auto.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "window/window_session_controller.h"

AvatarCornersPreview::AvatarCornersPreview(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _peer(controller->session().user().get()) {
	const auto &row = st::defaultDialogRow;
	setFixedHeight(row.height);
	_peer->loadUserpic();
	AyuSettings::getInstance().localOwnedGiftsValue(
		controller->session().userId().bare
	) | rpl::on_next([=](const QStringList &gifts) {
		auto emojis = QString();
		for (const auto &gift : gifts) {
			if (gift == u"teddy"_q) {
				emojis += u"🧸"_q;
			} else if (gift == u"cookie"_q) {
				emojis += u"🍪"_q;
			} else if (gift == u"lollipop"_q) {
				emojis += u"🍭"_q;
			} else if (gift == u"flamingo"_q) {
				emojis += u"🦩"_q;
			} else if (gift == u"backpack"_q) {
				emojis += u"🎒"_q;
			} else if (gift == u"lamp"_q) {
				emojis += u"🏮"_q;
			} else if (gift.startsWith(u"telegram:"_q)) {
				emojis += u"🎁"_q;
			}
		}
		auto subtitle = emojis.isEmpty()
			? rpl::single(tr::hoursgram_ProfilePreview(tr::now))
			: tr::hoursgram_ProfileGifts(lt_item, rpl::single(emojis));
		std::move(subtitle) | rpl::on_next([=](QString text) {
			_subtitle = std::move(text);
			update();
		}, lifetime());
	}, lifetime());
}

void AvatarCornersPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto &row = st::defaultDialogRow;
	const auto photoSize = row.photoSize;
	const auto xShift = st::settingsButtonNoIcon.padding.left()
		- row.padding.left();
	const auto userpicX = row.padding.left() + xShift;
	const auto userpicY = (height() - photoSize) / 2;

	p.fillRect(rect(), st::windowBg);

	_peer->paintUserpicLeft(
		p, _userpicView, userpicX, userpicY, width(), photoSize);

	const auto nameText = _peer->name();
	p.setPen(st::dialogsNameFg);
	p.setFont(st::semiboldFont);
	p.drawText(row.nameLeft + xShift, row.nameTop + st::semiboldFont->ascent, nameText);

	p.setPen(st::dialogsTextFg);
	p.setFont(st::dialogsTextFont);
	p.drawText(
		row.textLeft + xShift,
		row.textTop + st::dialogsTextFont->ascent,
		_subtitle);
}
