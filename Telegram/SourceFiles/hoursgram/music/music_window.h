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

// HorusGram Music: a window of its own with the fresh wave of new songs,
// search over the audio of public Telegram chats, the music of Saved
// Messages, playlists and the lyrics of the track that plays. Everything
// plays through the usual Telegram player, so the player bar in the chats
// keeps working with it. Opening it again raises the window that is open.
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

} // namespace HoursGram::Music
