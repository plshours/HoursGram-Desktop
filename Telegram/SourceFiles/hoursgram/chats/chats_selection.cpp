/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/chats/chats_selection.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "hoursgram/music/music_common.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_hoursgram_design.h"
#include "styles/style_layers.h"

#include <QtGui/QPainterPath>

namespace HoursGram::Chats {
namespace {

using HoursGram::Music::Tr;

// One request after another with a pause, so deleting many chats at once
// does not run into the flood limits of the server.
constexpr auto kDeleteStep = crl::time(350);

void DeleteChatsBox(
		not_null<Ui::GenericBox*> box,
		std::vector<not_null<PeerData*>> peers,
		Fn<void()> done) {
	box->setTitle(rpl::single(Tr("Delete chats", "Удалить чаты")));
	const auto content = box->verticalLayout();
	const auto users = ranges::count_if(peers, [](not_null<PeerData*> peer) {
		return peer->isUser() && !peer->isSelf();
	});
	Ui::AddSkip(content);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			Tr(
				"Delete the chats you picked (%1)? You will leave the "
				"groups and channels among them.",
				"Удалить выбранные чаты (%1)? Из групп и каналов среди них "
				"вы выйдете.").arg(int(peers.size())),
			st::boxLabel),
		st::boxRowPadding);
	auto revoke = (Ui::Checkbox*)nullptr;
	if (users > 0) {
		Ui::AddSkip(content);
		revoke = content->add(
			object_ptr<Ui::Checkbox>(
				content,
				Tr(
					"Also delete for the other side in private chats",
					"Удалить и у собеседников в личных чатах"),
				false,
				st::defaultBoxCheckbox),
			st::boxRowPadding);
	}
	Ui::AddSkip(content);

	box->addButton(rpl::single(Tr("Delete", "Удалить")), [=] {
		const auto both = revoke && revoke->checked();
		auto delay = crl::time(0);
		for (const auto &peer : peers) {
			const auto session = &peer->session();
			base::call_delayed(delay, session, [=] {
				Core::App().closeChatFromWindows(peer);
				session->api().deleteConversation(
					peer,
					both && peer->isUser());
			});
			delay += kDeleteStep;
		}
		if (done) {
			done();
		}
		box->closeBox();
	}, st::attentionBoxButton);
	box->addButton(rpl::single(Tr("Cancel", "Отмена")), [=] {
		box->closeBox();
	});
}

} // namespace

not_null<Ui::RpWidget*> CreateSelectionBar(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<int> count,
		Fn<std::vector<not_null<History*>>()> selected,
		Fn<void()> clear) {
	const auto bar = Ui::CreateChild<Ui::RpWidget>(parent.get());
	bar->resize(bar->width(), st::hoursgramSelectionBarHeight);
	bar->hide();

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		bar,
		QString(),
		st::boxLabel);
	const auto read = Ui::CreateChild<Ui::RoundButton>(
		bar,
		rpl::single(Tr("Read", "Прочитать")),
		st::hoursgramSelectionButton);
	const auto remove = Ui::CreateChild<Ui::RoundButton>(
		bar,
		rpl::single(Tr("Delete", "Удалить")),
		st::hoursgramSelectionDeleteButton);
	const auto cancel = Ui::CreateChild<Ui::RoundButton>(
		bar,
		rpl::single(Tr("Cancel", "Отмена")),
		st::hoursgramSelectionButton);
	for (const auto button : { read, remove, cancel }) {
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	}
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	read->setClickedCallback([=] {
		for (const auto &history : selected()) {
			Window::MarkAsReadThread(history);
		}
		clear();
	});
	remove->setClickedCallback([=] {
		auto peers = std::vector<not_null<PeerData*>>();
		for (const auto &history : selected()) {
			peers.push_back(history->peer);
		}
		if (!peers.empty()) {
			controller->show(Box(DeleteChatsBox, std::move(peers), clear));
		}
	});
	cancel->setClickedCallback(clear);

	bar->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(bar);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::hoursgramSelectionBarRadius;
		p.setPen(QPen(st::windowBgRipple->c, st::lineWidth));
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			QRectF(bar->rect()).marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }),
			radius,
			radius);
	}, bar->lifetime());

	bar->sizeValue() | rpl::on_next([=](QSize size) {
		const auto padding = st::hoursgramSelectionBarPadding;
		auto right = size.width() - padding.right();
		const auto skip = st::hoursgramSelectionBarPadding.right() / 2;
		for (const auto button : { cancel, remove, read }) {
			button->moveToLeft(
				right - button->width(),
				(size.height() - button->height()) / 2,
				size.width());
			right -= button->width() + skip;
		}
		label->resizeToWidth(std::max(right - padding.left() - skip, 1));
		label->moveToLeft(
			padding.left(),
			(size.height() - label->height()) / 2,
			size.width());
	}, bar->lifetime());

	std::move(count) | rpl::on_next([=](int value) {
		label->setText(Tr("Picked: %1", "Выбрано: %1").arg(value));
		bar->resizeToWidth(bar->width());
		bar->setVisible(value > 0);
		if (value > 0) {
			bar->raise();
		}
	}, bar->lifetime());

	return bar;
}

void PaintSelectionCheck(QPainter &p, QRect userpic) {
	const auto size = st::hoursgramSelectionCheckSize;
	const auto line = float64(st::lineWidth) * 2;
	const auto rect = QRectF(
		userpic.x() + userpic.width() - size + line,
		userpic.y() + userpic.height() - size + line,
		size,
		size);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(st::windowBg->c, line));
	p.setBrush(st::windowBgActive);
	p.drawEllipse(rect);

	auto path = QPainterPath();
	path.moveTo(rect.x() + size * 0.28, rect.y() + size * 0.52);
	path.lineTo(rect.x() + size * 0.44, rect.y() + size * 0.67);
	path.lineTo(rect.x() + size * 0.73, rect.y() + size * 0.36);
	p.setPen(QPen(
		st::windowFgActive->c,
		line,
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
}

} // namespace HoursGram::Chats
