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

enum class Section {
	Home,
	Search,
	Saved,
	NowPlaying,
	Playlist,
};

// HorusGram Music: the fresh wave of new songs, search over the audio of
// public Telegram chats, the music of Saved Messages, playlists and the
// lyrics of the track that plays. It opens in the main window in place of
// the chats, with the folders at the side. Everything plays through the
// usual Telegram player, so the player bar in the chats keeps working.
void ShowMusic(
	not_null<Window::SessionController*> controller,
	Section section = Section::Home,
	uint64 playlistId = 0);

// The same window on the page of the track that plays, with its lyrics.
void ShowPlayer(not_null<Window::SessionController*> controller);

// Starts the track when one is passed and opens the window on its page.
void ShowPlayerWindow(
	not_null<Window::SessionController*> controller,
	const AudioMsgId &audioId);

// A small window over everything with the cover and the line of the lyrics
// that plays, like karaoke; it is dragged anywhere and snaps into corners.
// Closes it when it is open.
void ToggleKaraoke(not_null<Window::SessionController*> controller);

} // namespace HoursGram::Music
