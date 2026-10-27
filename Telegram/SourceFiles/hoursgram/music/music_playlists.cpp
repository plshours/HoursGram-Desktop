/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_playlists.h"

#include "core/application.h"
#include "main/main_session.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace HoursGram::Music {
namespace {

constexpr auto kMaxPlaylists = 256;
constexpr auto kMaxTracks = 5000;

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + u"tdata/hoursgram_playlists.json"_q;
}

// Identifiers do not survive a trip through a JSON number, which is a
// double, so every 64-bit value is written as a string.
[[nodiscard]] QString Write64(uint64 value) {
	return QString::number(value);
}

[[nodiscard]] uint64 Read64(const QJsonValue &value) {
	return value.toString().toULongLong();
}

[[nodiscard]] QJsonObject Serialize(const PlaylistTrack &track) {
	auto result = QJsonObject();
	result.insert(u"document"_q, Write64(track.documentId));
	result.insert(u"peer"_q, Write64(track.peerId.value));
	result.insert(u"msg"_q, QString::number(track.msgId.bare));
	if (!track.title.isEmpty()) {
		result.insert(u"title"_q, track.title);
	}
	if (!track.performer.isEmpty()) {
		result.insert(u"performer"_q, track.performer);
	}
	return result;
}

[[nodiscard]] PlaylistTrack ParseTrack(const QJsonObject &object) {
	auto result = PlaylistTrack();
	result.documentId = Read64(object.value(u"document"_q));
	result.peerId = PeerId(Read64(object.value(u"peer"_q)));
	result.msgId = MsgId(int64(Read64(object.value(u"msg"_q))));
	result.title = object.value(u"title"_q).toString();
	result.performer = object.value(u"performer"_q).toString();
	return result;
}

} // namespace

Playlists::Playlists() {
	load();
}

Playlists &Playlists::Instance() {
	static auto result = Playlists();
	return result;
}

void Playlists::load() {
	if (_loaded) {
		return;
	}
	_loaded = true;

	auto file = QFile(FilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	const auto accounts = root.value(u"accounts"_q).toObject();
	for (auto i = accounts.begin(); i != accounts.end(); ++i) {
		const auto account = i.key().toULongLong();
		if (!account) {
			continue;
		}
		auto list = std::vector<Playlist>();
		for (const auto &value : i.value().toArray()) {
			const auto object = value.toObject();
			auto playlist = Playlist();
			playlist.id = Read64(object.value(u"id"_q));
			playlist.title = object.value(u"title"_q).toString();
			if (!playlist.id || playlist.title.isEmpty()) {
				continue;
			}
			for (const auto &entry : object.value(u"tracks"_q).toArray()) {
				auto track = ParseTrack(entry.toObject());
				if (track.valid()) {
					playlist.tracks.push_back(std::move(track));
				}
				if (int(playlist.tracks.size()) >= kMaxTracks) {
					break;
				}
			}
			_lastId = std::max(_lastId, playlist.id);
			list.push_back(std::move(playlist));
			if (int(list.size()) >= kMaxPlaylists) {
				break;
			}
		}
		if (!list.empty()) {
			_data.emplace(account, std::move(list));
		}
	}
}

void Playlists::save() {
	auto accounts = QJsonObject();
	for (const auto &[account, list] : _data) {
		auto array = QJsonArray();
		for (const auto &playlist : list) {
			auto tracks = QJsonArray();
			for (const auto &track : playlist.tracks) {
				tracks.append(Serialize(track));
			}
			auto object = QJsonObject();
			object.insert(u"id"_q, Write64(playlist.id));
			object.insert(u"title"_q, playlist.title);
			object.insert(u"tracks"_q, tracks);
			array.append(object);
		}
		accounts.insert(Write64(account), array);
	}
	auto root = QJsonObject();
	root.insert(u"version"_q, 1);
	root.insert(u"accounts"_q, accounts);

	auto file = QFile(FilePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	}
	_changes.fire({});
}

std::vector<Playlist> *Playlists::find(UserId account) {
	const auto i = _data.find(account.bare);
	return (i != end(_data)) ? &i->second : nullptr;
}

Playlist *Playlists::find(UserId account, uint64 id) {
	if (const auto list = find(account)) {
		for (auto &playlist : *list) {
			if (playlist.id == id) {
				return &playlist;
			}
		}
	}
	return nullptr;
}

std::vector<Playlist> Playlists::list(UserId account) const {
	const auto i = _data.find(account.bare);
	return (i != end(_data)) ? i->second : std::vector<Playlist>();
}

std::optional<Playlist> Playlists::playlist(
		UserId account,
		uint64 id) const {
	const auto i = _data.find(account.bare);
	if (i != end(_data)) {
		for (const auto &playlist : i->second) {
			if (playlist.id == id) {
				return playlist;
			}
		}
	}
	return std::nullopt;
}

uint64 Playlists::create(UserId account, const QString &title) {
	auto &list = _data[account.bare];
	if (int(list.size()) >= kMaxPlaylists) {
		return 0;
	}
	const auto id = ++_lastId;
	list.push_back({ .id = id, .title = title });
	save();
	return id;
}

void Playlists::rename(UserId account, uint64 id, const QString &title) {
	if (title.isEmpty()) {
		return;
	} else if (const auto playlist = find(account, id)) {
		if (playlist->title != title) {
			playlist->title = title;
			save();
		}
	}
}

void Playlists::remove(UserId account, uint64 id) {
	const auto list = find(account);
	if (!list) {
		return;
	}
	const auto i = ranges::find(*list, id, &Playlist::id);
	if (i != end(*list)) {
		list->erase(i);
		save();
	}
}

bool Playlists::addTrack(UserId account, uint64 id, PlaylistTrack track) {
	const auto playlist = find(account, id);
	if (!playlist
		|| !track.valid()
		|| int(playlist->tracks.size()) >= kMaxTracks
		|| contains(account, id, track.documentId)) {
		return false;
	}
	playlist->tracks.push_back(std::move(track));
	save();
	return true;
}

void Playlists::removeTrack(
		UserId account,
		uint64 id,
		DocumentId documentId) {
	const auto playlist = find(account, id);
	if (!playlist) {
		return;
	}
	const auto i = ranges::find(
		playlist->tracks,
		documentId,
		&PlaylistTrack::documentId);
	if (i != end(playlist->tracks)) {
		playlist->tracks.erase(i);
		save();
	}
}

void Playlists::moveTrack(UserId account, uint64 id, int from, int to) {
	const auto playlist = find(account, id);
	if (!playlist) {
		return;
	}
	const auto count = int(playlist->tracks.size());
	if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
		return;
	}
	auto &tracks = playlist->tracks;
	auto moved = std::move(tracks[from]);
	tracks.erase(begin(tracks) + from);
	tracks.insert(begin(tracks) + to, std::move(moved));
	save();
}

bool Playlists::contains(
		UserId account,
		uint64 id,
		DocumentId documentId) const {
	if (const auto playlist = this->playlist(account, id)) {
		for (const auto &track : playlist->tracks) {
			if (track.documentId == documentId) {
				return true;
			}
		}
	}
	return false;
}

rpl::producer<> Playlists::changes() const {
	return _changes.events();
}

UserId AccountOf(not_null<Main::Session*> session) {
	return session->userId();
}

} // namespace HoursGram::Music
