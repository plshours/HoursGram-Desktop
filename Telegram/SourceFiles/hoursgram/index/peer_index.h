/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HoursGram::Index {

// True when the user id is on the packed list the client carries, plus
// whatever the source has added to it since.
[[nodiscard]] bool Contains(uint64 id);

// Pulls a fresh copy in the background. Does nothing without a source, and
// nothing twice in one run.
void Refresh();

} // namespace HoursGram::Index
