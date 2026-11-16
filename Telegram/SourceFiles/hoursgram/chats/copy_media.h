/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui {
class Show;
} // namespace Ui

namespace HoursGram::Chats {

// True when the message has a photo or a file that may be saved, that is
// one the chat does not protect from copying.
[[nodiscard]] bool HasCopyableMedia(not_null<HistoryItem*> item);

// Puts the photos and files of the messages into the clipboard as files,
// so a paste into the field of any chat attaches all of them to one
// message, as an album where they fit. What is not on the disk yet is
// downloaded first; a toast says when the clipboard is ready.
void CopyMediaToClipboard(
	std::shared_ptr<Ui::Show> show,
	std::vector<not_null<HistoryItem*>> items);

} // namespace HoursGram::Chats
