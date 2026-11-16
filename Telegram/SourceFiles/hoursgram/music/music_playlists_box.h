/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class AudioMsgId;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HoursGram::Music {

// The playlists live in the music window now; these open it.
void ShowPlaylists(not_null<Window::SessionController*> controller);
void ShowPlaylist(
	not_null<Window::SessionController*> controller,
	uint64 playlistId);

// Asks which playlist the track should go to, offering to start a new one.
void ShowAddToPlaylist(
	not_null<Window::SessionController*> controller,
	const AudioMsgId &audioId);
void ShowAddToPlaylist(
	std::shared_ptr<Ui::Show> show,
	not_null<Main::Session*> session,
	const AudioMsgId &audioId);

// Asks for a playlist name. `done` gets the trimmed, non-empty name.
void PromptPlaylistName(
	std::shared_ptr<Ui::Show> show,
	rpl::producer<QString> title,
	const QString &initial,
	Fn<void(QString)> done);

void ConfirmDeletePlaylist(
	std::shared_ptr<Ui::Show> show,
	not_null<Main::Session*> session,
	uint64 playlistId);

} // namespace HoursGram::Music
