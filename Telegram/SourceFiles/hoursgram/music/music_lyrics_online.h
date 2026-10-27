/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "hoursgram/music/music_lyrics.h"

class DocumentData;

namespace HoursGram::Music {

// Looks the track up in a public lyrics database when nothing was found on
// disk, sending it the performer, the title and the length of the track.
// Lyrics that are found are kept on disk, so a track is only ever looked up
// once; `done` runs on the main thread and only when something was found.
void SearchLyricsOnline(
	not_null<DocumentData*> document,
	Fn<void(Lyrics)> done);

} // namespace HoursGram::Music
