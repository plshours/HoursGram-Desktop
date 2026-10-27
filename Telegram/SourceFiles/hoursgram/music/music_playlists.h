/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

namespace Main {
class Session;
} // namespace Main

namespace HoursGram::Music {

// A track as it was when the user added it. The message it came from is
// remembered so the file can be opened and played later; the title and the
// performer are copied so a playlist stays readable even when the message
// is gone.
struct PlaylistTrack {
	DocumentId documentId = 0;
	PeerId peerId;
	MsgId msgId = 0;
	QString title;
	QString performer;

	[[nodiscard]] bool valid() const {
		return documentId != 0;
	}
};

struct Playlist {
	uint64 id = 0;
	QString title;
	std::vector<PlaylistTrack> tracks;
};

// Playlists live in tdata/hoursgram_playlists.json, grouped by account, and
// are saved whenever they change. They hold no media of their own, only
// references to messages the account can already see.
class Playlists final {
public:
	[[nodiscard]] static Playlists &Instance();

	[[nodiscard]] std::vector<Playlist> list(UserId account) const;
	[[nodiscard]] std::optional<Playlist> playlist(
		UserId account,
		uint64 id) const;

	[[nodiscard]] uint64 create(UserId account, const QString &title);
	void rename(UserId account, uint64 id, const QString &title);
	void remove(UserId account, uint64 id);

	// Adding a track that is already in the playlist does nothing and
	// returns false, so the caller can tell the user about it.
	bool addTrack(UserId account, uint64 id, PlaylistTrack track);
	void removeTrack(UserId account, uint64 id, DocumentId documentId);
	void moveTrack(UserId account, uint64 id, int from, int to);

	[[nodiscard]] bool contains(
		UserId account,
		uint64 id,
		DocumentId documentId) const;

	// Fires after any change, with no value, for views to reload from.
	[[nodiscard]] rpl::producer<> changes() const;

private:
	Playlists();

	void load();
	void save();

	[[nodiscard]] std::vector<Playlist> *find(UserId account);
	[[nodiscard]] Playlist *find(UserId account, uint64 id);

	base::flat_map<BareId, std::vector<Playlist>> _data;
	rpl::event_stream<> _changes;
	uint64 _lastId = 0;
	bool _loaded = false;

};

// Convenience wrappers for call sites that already hold a session.
[[nodiscard]] UserId AccountOf(not_null<Main::Session*> session);

} // namespace HoursGram::Music
