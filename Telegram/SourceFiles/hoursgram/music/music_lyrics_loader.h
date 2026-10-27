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

// Where a lyrics file attached to a track by the user is kept.
[[nodiscard]] QString LyricsStorePath(DocumentId id);

// Looks for lyrics attached to the track, then for an .lrc file sitting
// next to the downloaded audio file, then for lyrics found online
// earlier. Returns empty lyrics when there is nothing to show. The files
// involved are a few kilobytes, so this reads them directly instead of
// going through a worker thread.
[[nodiscard]] Lyrics LoadLyrics(not_null<DocumentData*> document);

// Where lyrics found online are kept, separately from the file the user
// attached, so that attaching and detaching stay under their control.
[[nodiscard]] QString LyricsCachePath(DocumentId id);

// Remembers lyrics found online for this track, so that the next time it
// plays they are read from disk and work without a connection.
bool CacheLyrics(DocumentId id, const QByteArray &bytes);

// Stores the contents of a lyrics file the user picked for this track.
// Returns false when the file could not be written.
bool AttachLyrics(DocumentId id, const QByteArray &bytes);

// Drops the lyrics file the user attached to this track, leaving any .lrc
// file next to the audio file alone.
void DetachLyrics(DocumentId id);

[[nodiscard]] bool LyricsAttached(DocumentId id);

} // namespace HoursGram::Music
