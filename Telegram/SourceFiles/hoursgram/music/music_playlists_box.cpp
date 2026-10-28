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
#include "hoursgram/music/music_playlists.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
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

// A playlist only remembers which message a track came from. The file can
// be played once that document is known to this account; until then the
// chat it was added from has to be opened once.
[[nodiscard]] AudioMsgId ResolveTrack(
		not_null<Main::Session*> session,
		const PlaylistTrack &track) {
	const auto document = session->data().document(track.documentId);
	if (!document->hasRemoteLocation() && document->filepath().isEmpty()) {
		return AudioMsgId();
	}
	return AudioMsgId(document, FullMsgId(track.peerId, track.msgId));
}

[[nodiscard]] DocumentId PlayingNow() {
	const auto current = Media::Player::instance()->current(
		AudioMsgId::Type::Song);
	return current.audio() ? current.audio()->id : DocumentId(0);
}

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

[[nodiscard]] QString PerformerText(const PlaylistTrack &track) {
	return track.performer.isEmpty()
		? tr::hoursgram_PlayerUnknownArtist(tr::now)
		: track.performer;
}

[[nodiscard]] QString TitleText(const PlaylistTrack &track) {
	return track.title.isEmpty()
		? tr::hoursgram_PlayerUnknownTrack(tr::now)
		: track.title;
}

void PromptName(
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		const QString &initial,
		Fn<void(QString)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::duplicate(title));
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
			done(value);
			box->closeBox();
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

} // namespace

void ShowPlaylists(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::hoursgram_Playlists());
		box->setWidth(st::boxWidth);
		box->setMaxHeight(st::hoursgramPlaylistMaxHeight);

		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto session = &controller->session();
		const auto account = AccountOf(session);
		const auto container = box->verticalLayout();
		const auto inner = container->add(
			object_ptr<Ui::VerticalLayout>(container));

		const auto rebuild = [=] {
			inner->clear();
			const auto list = Playlists::Instance().list(account);
			if (list.empty()) {
				AddEmpty(inner, tr::hoursgram_PlaylistsEmpty());
				return;
			}
			for (const auto &playlist : list) {
				const auto id = playlist.id;
				const auto title = playlist.title;
				const auto row = inner->add(object_ptr<Row>(inner, true));
				row->setName(title);
				row->setStatus(CountText(int(playlist.tracks.size())));
				row->setClickedCallback([=] {
					ShowPlaylist(controller, id);
				});
				row->menu()->setClickedCallback([=] {
					state->menu = base::make_unique_q<Ui::PopupMenu>(
						row,
						st::popupMenuWithIcons);
					const auto menu = state->menu.get();
					menu->addAction(
						tr::hoursgram_PlaylistRename(tr::now),
						[=] {
							PromptName(
								controller,
								tr::hoursgram_PlaylistRename(),
								title,
								[=](QString name) {
									Playlists::Instance().rename(
										account,
										id,
										name);
								});
						},
						&st::menuIconEdit);
					menu->addAction(
						tr::hoursgram_PlaylistDelete(tr::now),
						[=] {
							controller->show(Ui::MakeConfirmBox({
								.text = tr::hoursgram_PlaylistDeleteSure(
									tr::now,
									lt_name,
									title),
								.confirmed = [=](Fn<void()> &&close) {
									close();
									Playlists::Instance().remove(account, id);
								},
								.confirmText = tr::lng_box_delete(),
								.confirmStyle = &st::attentionBoxButton,
							}));
						},
						&st::menuIconDelete);
					menu->popup(QCursor::pos());
				});
			}
		};
		rebuild();

		Playlists::Instance().changes() | rpl::on_next([=] {
			rebuild();
		}, box->lifetime());

		box->addLeftButton(tr::hoursgram_PlaylistNew(), [=] {
			PromptName(
				controller,
				tr::hoursgram_PlaylistNew(),
				QString(),
				[=](QString name) {
					Playlists::Instance().create(account, name);
				});
		});
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowPlaylist(
		not_null<Window::SessionController*> controller,
		uint64 playlistId) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
			std::vector<std::pair<DocumentId, Row*>> rows;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto session = &controller->session();
		const auto account = AccountOf(session);
		const auto container = box->verticalLayout();
		const auto inner = container->add(
			object_ptr<Ui::VerticalLayout>(container));

		box->setWidth(st::boxWidth);
		box->setMaxHeight(st::hoursgramPlaylistMaxHeight);

		const auto refreshPlaying = [=] {
			const auto playing = PlayingNow();
			for (const auto &pair : state->rows) {
				pair.second->setPlaying(playing && pair.first == playing);
			}
		};

		const auto rebuild = [=] {
			const auto playlist = Playlists::Instance().playlist(
				account,
				playlistId);
			if (!playlist) {
				box->closeBox();
				return;
			}
			box->setTitle(rpl::single(playlist->title));
			state->rows.clear();
			inner->clear();
			if (playlist->tracks.empty()) {
				AddEmpty(inner, tr::hoursgram_PlaylistEmpty());
				return;
			}
			for (const auto &track : playlist->tracks) {
				const auto documentId = track.documentId;
				const auto row = inner->add(object_ptr<Row>(inner, true));
				row->setName(TitleText(track));
				row->setStatus(PerformerText(track));
				row->setDimmed(!ResolveTrack(session, track).audio());
				row->setClickedCallback([=] {
					const auto audioId = ResolveTrack(session, track);
					if (!audioId.audio()) {
						controller->showToast(
							tr::hoursgram_PlaylistTrackMissing(tr::now));
						return;
					}
					Media::Player::instance()->play(audioId);
				});
				row->menu()->setClickedCallback([=] {
					state->menu = base::make_unique_q<Ui::PopupMenu>(
						row,
						st::popupMenuWithIcons);
					const auto menu = state->menu.get();
					menu->addAction(
						tr::hoursgram_PlaylistRemoveTrack(tr::now),
						[=] {
							Playlists::Instance().removeTrack(
								account,
								playlistId,
								documentId);
						},
						&st::menuIconDelete);
					menu->popup(QCursor::pos());
				});
				state->rows.push_back({ documentId, row });
			}
			refreshPlaying();
		};
		rebuild();

		Playlists::Instance().changes() | rpl::on_next([=] {
			rebuild();
		}, box->lifetime());

		Media::Player::instance()->updatedNotifier(
		) | rpl::on_next([=](const Media::Player::TrackState &track) {
			if (track.id.type() == AudioMsgId::Type::Song) {
				refreshPlaying();
			}
		}, box->lifetime());

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowAddToPlaylist(
		not_null<Window::SessionController*> controller,
		const AudioMsgId &audioId) {
	const auto document = audioId.audio();
	if (!document) {
		return;
	}
	auto track = PlaylistTrack();
	track.documentId = document->id;
	track.peerId = audioId.contextId().peer;
	track.msgId = audioId.contextId().msg;
	if (const auto song = document->song()) {
		track.title = song->title;
		track.performer = song->performer;
	}
	if (track.title.isEmpty()) {
		track.title = document->filename();
	}

	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::hoursgram_AddToPlaylist());
		box->setWidth(st::boxWidth);
		box->setMaxHeight(st::hoursgramPlaylistMaxHeight);

		const auto session = &controller->session();
		const auto account = AccountOf(session);
		const auto container = box->verticalLayout();
		const auto inner = container->add(
			object_ptr<Ui::VerticalLayout>(container));

		const auto add = [=](uint64 playlistId, const QString &name) {
			if (Playlists::Instance().addTrack(account, playlistId, track)) {
				controller->showToast(tr::hoursgram_PlaylistAdded(
					tr::now,
					lt_name,
					name));
			} else {
				controller->showToast(tr::hoursgram_PlaylistAlready(
					tr::now,
					lt_name,
					name));
			}
			box->closeBox();
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
				row->setClickedCallback([=] {
					add(id, name);
				});
			}
		};
		rebuild();

		Playlists::Instance().changes() | rpl::on_next([=] {
			rebuild();
		}, box->lifetime());

		box->addLeftButton(tr::hoursgram_PlaylistNew(), [=] {
			PromptName(
				controller,
				tr::hoursgram_PlaylistNew(),
				QString(),
				[=](QString name) {
					const auto id = Playlists::Instance().create(
						account,
						name);
					if (id) {
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
