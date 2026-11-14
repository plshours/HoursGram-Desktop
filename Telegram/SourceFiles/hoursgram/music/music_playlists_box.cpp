/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_playlists_box.h"

#include "base/unique_qptr.h"
#include "data/data_audio_msg_id.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "hoursgram/music/music_common.h"
#include "hoursgram/music/music_playlists.h"
#include "hoursgram/music/music_window.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_hoursgram_music.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QCursor>
#include <QtGui/QLinearGradient>

namespace HoursGram::Music {
namespace {

// One line of either list: a rounded tile on the left, the name over a
// quieter second line, and the menu dots on the right. A playlist and a
// track inside it are drawn the same way on purpose.
class Row final : public Ui::RippleButton {
public:
	Row(QWidget *parent, bool withMenu);

	void setName(const QString &name);
	void setStatus(const QString &status);
	void setPlaying(bool playing);
	void setDimmed(bool dimmed);

	[[nodiscard]] Ui::IconButton *menu() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void paintTile(QPainter &p);

	QString _name;
	QString _status;
	object_ptr<Ui::IconButton> _menu;
	bool _withMenu = false;
	bool _playing = false;
	bool _dimmed = false;

};

Row::Row(QWidget *parent, bool withMenu)
: RippleButton(parent, st::hoursgramPlaylistRipple)
, _menu(this, st::hoursgramPlaylistMenu)
, _withMenu(withMenu) {
	resize(width(), st::hoursgramPlaylistRowHeight);
	if (!_withMenu) {
		_menu->hide();
	}
}

void Row::setName(const QString &name) {
	if (_name != name) {
		_name = name;
		update();
	}
}

void Row::setStatus(const QString &status) {
	if (_status != status) {
		_status = status;
		update();
	}
}

void Row::setPlaying(bool playing) {
	if (_playing != playing) {
		_playing = playing;
		update();
	}
}

void Row::setDimmed(bool dimmed) {
	if (_dimmed != dimmed) {
		_dimmed = dimmed;
		update();
	}
}

Ui::IconButton *Row::menu() const {
	return _menu.data();
}

void Row::resizeEvent(QResizeEvent *e) {
	_menu->moveToRight(
		st::hoursgramPlaylistMenuSkip,
		(height() - _menu->height()) / 2,
		width());
}

void Row::paintTile(QPainter &p) {
	const auto size = st::hoursgramPlaylistCoverSize;
	const auto tile = QRect(
		st::hoursgramPlaylistRowPadding.left(),
		(height() - size) / 2,
		size,
		size);

	p.setPen(Qt::NoPen);
	p.setBrush(_dimmed ? st::windowBgRipple->b : st::windowBgActive->b);
	p.drawRoundedRect(
		tile,
		st::hoursgramPlaylistCoverRadius,
		st::hoursgramPlaylistCoverRadius);

	const auto &icon = _playing
		? st::hoursgramPlaylistPauseIcon
		: _withMenu
		? st::hoursgramPlaylistPlayIcon
		: st::hoursgramPlaylistTileIcon;
	icon.paint(
		p,
		tile.x() + (size - icon.width()) / 2,
		tile.y() + (size - icon.height()) / 2,
		width());
}

void Row::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	if (isOver() || isDown()) {
		p.fillRect(e->rect(), st::windowBgOver);
	}
	paintRipple(p, 0, 0);

	{
		auto hq = PainterHighQualityEnabler(p);
		paintTile(p);
	}

	const auto left = st::hoursgramPlaylistRowPadding.left()
		+ st::hoursgramPlaylistCoverSize
		+ st::hoursgramPlaylistCoverSkip;
	const auto right = st::hoursgramPlaylistRowPadding.right()
		+ (_withMenu ? _menu->width() : 0);
	const auto available = width() - left - right;
	if (available <= 0) {
		return;
	}

	p.setPen(_playing
		? st::windowActiveTextFg
		: _dimmed
		? st::windowSubTextFg
		: st::windowBoldFg);
	p.setFont(st::hoursgramPlaylistName.font);
	p.drawTextLeft(
		left,
		st::hoursgramPlaylistNameTop,
		width(),
		st::hoursgramPlaylistName.font->elided(_name, available));

	if (_status.isEmpty()) {
		return;
	}
	p.setPen(st::windowSubTextFg);
	p.setFont(st::hoursgramPlaylistStatus.font);
	p.drawTextLeft(
		left,
		st::hoursgramPlaylistStatusTop,
		width(),
		st::hoursgramPlaylistStatus.font->elided(_status, available));
}

void AddEmpty(
		not_null<Ui::VerticalLayout*> inner,
		rpl::producer<QString> text) {
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			std::move(text),
			st::hoursgramPlaylistEmpty),
		st::hoursgramPlaylistEmptyPadding);
}

[[nodiscard]] QString CountText(int count) {
	return count
		? tr::hoursgram_PlaylistTracks(tr::now, lt_count, count)
		: tr::hoursgram_PlaylistNoTracks(tr::now);
}

} // namespace

void ShowPlaylists(not_null<Window::SessionController*> controller) {
	ShowMusic(controller, Section::Home);
}

void ShowPlaylist(
		not_null<Window::SessionController*> controller,
		uint64 playlistId) {
	ShowMusic(controller, Section::Playlist, playlistId);
}

void PromptPlaylistName(
		std::shared_ptr<Ui::Show> show,
		rpl::producer<QString> title,
		const QString &initial,
		Fn<void(QString)> done) {
	auto titleShared = std::make_shared<rpl::producer<QString>>(
		std::move(title));
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(std::move(*titleShared));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::hoursgram_PlaylistName(),
			initial));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto save = [=] {
			const auto value = field->getLastText().trimmed();
			if (value.isEmpty()) {
				field->showError();
				return;
			}
			box->closeBox();
			done(value);
		};
		field->submits() | rpl::on_next([=] {
			save();
		}, field->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ConfirmDeletePlaylist(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		uint64 playlistId) {
	const auto account = AccountOf(session);
	const auto playlist = Playlists::Instance().playlist(account, playlistId);
	if (!playlist) {
		return;
	}
	show->showBox(Ui::MakeConfirmBox({
		.text = tr::hoursgram_PlaylistDeleteSure(
			tr::now,
			lt_name,
			playlist->title),
		.confirmed = [=](Fn<void()> &&close) {
			close();
			Playlists::Instance().remove(account, playlistId);
		},
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void ShowAddToPlaylist(
		not_null<Window::SessionController*> controller,
		const AudioMsgId &audioId) {
	ShowAddToPlaylist(controller->uiShow(), &controller->session(), audioId);
}

void ShowAddToPlaylist(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const AudioMsgId &audioId) {
	const auto document = audioId.audio();
	if (!document) {
		return;
	}
	auto track = PlaylistTrack();
	track.documentId = document->id;
	track.peerId = audioId.contextId().peer;
	track.msgId = audioId.contextId().msg;
	track.title = TrackTitle(document);
	if (const auto song = document->song()) {
		track.performer = song->performer;
	}

	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::hoursgram_AddToPlaylist());
		box->setWidth(st::boxWidth);
		box->setMaxHeight(st::hoursgramPlaylistMaxHeight);

		const auto account = AccountOf(session);
		const auto container = box->verticalLayout();
		const auto inner = container->add(
			object_ptr<Ui::VerticalLayout>(container));

		const auto add = [=](uint64 playlistId, const QString &name) {
			if (Playlists::Instance().addTrack(account, playlistId, track)) {
				show->showToast(tr::hoursgram_PlaylistAdded(
					tr::now,
					lt_name,
					name));
			} else {
				show->showToast(tr::hoursgram_PlaylistAlready(
					tr::now,
					lt_name,
					name));
			}
		};

		const auto rebuild = [=] {
			inner->clear();
			const auto list = Playlists::Instance().list(account);
			if (list.empty()) {
				AddEmpty(inner, tr::hoursgram_PlaylistsEmpty());
				return;
			}
			for (const auto &playlist : list) {
				const auto id = playlist.id;
				const auto name = playlist.title;
				const auto row = inner->add(object_ptr<Row>(inner, false));
				row->setName(name);
				row->setStatus(CountText(int(playlist.tracks.size())));
				row->setDimmed(Playlists::Instance().contains(
					account,
					id,
					track.documentId));
				row->setClickedCallback([=] {
					box->closeBox();
					add(id, name);
				});
			}
		};
		rebuild();

		Playlists::Instance().changes() | rpl::on_next([=] {
			rebuild();
		}, box->lifetime());

		box->addLeftButton(tr::hoursgram_PlaylistNew(), [=] {
			PromptPlaylistName(
				show,
				tr::hoursgram_PlaylistNew(),
				QString(),
				[=](QString name) {
					const auto id = Playlists::Instance().create(
						account,
						name);
					if (id) {
						box->closeBox();
						add(id, name);
					}
				});
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace HoursGram::Music
