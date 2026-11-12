/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_audio_msg_id.h"
#include "hoursgram/music/music_sources.h"

namespace Main {
class Session;
} // namespace Main

namespace HoursGram::Music {

// One entry of what the music window plays: either an audio file the
// session already knows, or a chart song that is looked up in Telegram
// right before it is played.
struct QueueTrack {
	AudioMsgId audio;
	ChartTrack chart;

	[[nodiscard]] bool resolved() const {
		return audio.audio() != nullptr;
	}
};

// The Telegram player walks over the shared media of the chat a track came
// from. A track started from the music window belongs to a list of the
// window instead: the search results, a playlist or the fresh wave. While
// the track that plays is one of that list, the player asks this queue for
// the next and the previous one; anything played from a chat leaves it.
class Queue final {
public:
	[[nodiscard]] static Queue &Instance();

	void start(
		not_null<Main::Session*> session,
		std::vector<QueueTrack> tracks,
		int index);
	void forget();

	[[nodiscard]] bool owns(const AudioMsgId &audio) const;
	[[nodiscard]] bool available(int delta, bool repeatAll) const;
	bool move(int delta, bool repeatAll, bool shuffle);

	[[nodiscard]] const std::vector<QueueTrack> &tracks() const {
		return _tracks;
	}
	[[nodiscard]] int index() const {
		return _index;
	}
	[[nodiscard]] bool resolving() const {
		return _resolving;
	}

	// Fires when the list, the position in it or the lookup state change.
	[[nodiscard]] rpl::producer<> changes() const;
	// Fires when a chart song could not be found among Telegram audio.
	[[nodiscard]] rpl::producer<ChartTrack> notFound() const;

private:
	Queue() = default;

	void playAt(int index, int direction, int attemptsLeft);
	[[nodiscard]] int stepFrom(int index, int delta, bool repeatAll) const;

	base::weak_ptr<Main::Session> _session;
	std::vector<QueueTrack> _tracks;
	AudioMsgId _current;
	int _index = -1;
	uint64 _generation = 0;
	bool _resolving = false;
	rpl::event_stream<> _changes;
	rpl::event_stream<ChartTrack> _notFound;

};

} // namespace HoursGram::Music
