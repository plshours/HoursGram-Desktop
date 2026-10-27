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

// The full player screen: cover, playback controls and the lyrics of the
// track that is playing, following the playback position when the lyrics
// file carries timings.
void ShowPlayer(not_null<Window::SessionController*> controller);

// The same player in a window of its own, so that it can stay open next to
// the chats. Starts playing the track when one is passed.
void ShowPlayerWindow(
	not_null<Window::SessionController*> controller,
	const AudioMsgId &audioId);

} // namespace HoursGram::Music
