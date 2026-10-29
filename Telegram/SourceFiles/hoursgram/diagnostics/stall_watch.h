/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HoursGram::Diagnostics {

// Called from the main thread around every event the application handles.
// When one of them takes long, a second thread writes the call stack of the
// main thread to the log, with function names if the .pdb is next to the
// executable. Nothing happens on other platforms.
void EventStarted();
void EventFinished();

} // namespace HoursGram::Diagnostics
