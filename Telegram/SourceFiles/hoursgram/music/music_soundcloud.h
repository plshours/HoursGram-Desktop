/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
/*
This file is derived from mokaGram, https://github.com/ecronxy/mokagram
(c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
*/
/*
SoundCloud integration notice: this is a plain HTTPS client for the
public SoundCloud web API used by the official web player — anonymous
access only, no login or authentication bypass, no handling of
protected or encrypted content, playback-oriented caching in the app
cache directory, no bulk scraping; rate limits are respected and
surfaced as errors. Not affiliated with or endorsed by SoundCloud
Ltd.; "SoundCloud" is a trademark of SoundCloud Ltd., used solely to
identify the service this feature connects to. See music_soundcloud.cpp
for the full notice.
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>
#include <QtGui/QImage>
#include <vector>

namespace HoursGram::Music {

struct ScTrack {
	QString id;
	QString title;
	QString artist;
	QString artworkUrl;
	QString permalink;
	QString transcodingUrl;
	QString authorization;
	QString extension;
	int durationMs = 0;
	bool hls = false;
};

struct ScSearchResult {
	std::vector<ScTrack> tracks;
	QString error;
	int nextOffset = 0;
};

void SoundCloudSearch(
	const QString &query,
	int offset,
	Fn<void(ScSearchResult)> done);

void SoundCloudResolve(
	const ScTrack &track,
	Fn<void(ScTrack, const QString &error)> done);

void SoundCloudDownload(
	const ScTrack &track,
	Fn<void(const QString &path, const QString &error)> done);

void SoundCloudLoadImage(const QString &url, Fn<void(QImage)> done);

[[nodiscard]] QString SoundCloudCacheDir();

} // namespace HoursGram::Music
