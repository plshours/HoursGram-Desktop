/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class AudioMsgId;

namespace Window {
class SessionController;
} // namespace Window

namespace HoursGram::Music {

// The list of playlists of the current account.
void ShowPlaylists(not_null<Window::SessionController*> controller);

// The tracks of one playlist, in playback order.
void ShowPlaylist(
	not_null<Window::SessionController*> controller,
	uint64 playlistId);

// Asks which playlist the track should go to, offering to start a new one.
void ShowAddToPlaylist(
	not_null<Window::SessionController*> controller,
	const AudioMsgId &audioId);

} // namespace HoursGram::Music
