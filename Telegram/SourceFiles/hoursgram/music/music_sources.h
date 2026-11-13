/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QDate>
#include <QtGui/QImage>

class AudioMsgId;

namespace Main {
class Session;
} // namespace Main

namespace HoursGram::Music {

// A song from the public charts. It is only a name: the file itself is
// looked up among the audio posted in public Telegram chats when the user
// wants to hear it, so it plays through the usual Telegram player.
struct ChartTrack {
	QString title;
	QString artist;
	QString artworkUrl;
	QDate released;
	int rank = 0;
};

struct FreshWave {
	std::vector<ChartTrack> fresh;
	std::vector<ChartTrack> chart;
	bool failed = false;
};

// The most played songs of the Apple Music charts for the language of the
// interface, with the Deezer chart when those can not be loaded. "Fresh"
// are the ones that came out in the last weeks, newest first. The answer
// is kept for a while, so switching back to the home page costs nothing.
void LoadFreshWave(Fn<void(FreshWave)> done);

// Audio files found by Telegram global search among public chats, already
// known to the session, so they can be played right away.
void SearchMusic(
	not_null<Main::Session*> session,
	const QString &query,
	Fn<void(std::vector<AudioMsgId>)> done);

// Songs found by name in the catalogs of the streaming platforms: Apple
// Music and Deezer are asked at once and their answers are merged, the
// same song from both of them kept once. Like a chart song, each one is
// looked up among Telegram audio only when it is played.
void SearchCatalog(
	const QString &query,
	Fn<void(std::vector<ChartTrack>)> done);

// Audio files the user keeps in Saved Messages, newest first.
void LoadSavedMusic(
	not_null<Main::Session*> session,
	Fn<void(std::vector<AudioMsgId>)> done);

// Finds the chart song among the Telegram audio. Several spellings of the
// query are tried, from the full one to the bare title, and files of
// verified channels win among equal matches. `done` gets an empty
// AudioMsgId when nothing close enough was found.
void ResolveChartTrack(
	not_null<Main::Session*> session,
	const ChartTrack &track,
	Fn<void(AudioMsgId)> done);

// Starts downloading the file of the track into the cache, so that it plays
// at once when its turn comes.
void Prefetch(const AudioMsgId &audio);

// Downloads cover art by URL, at most once per run for every address.
// `done` runs on the main thread, with a null image when it failed.
void LoadArtwork(const QString &url, Fn<void(QImage)> done);

} // namespace HoursGram::Music
