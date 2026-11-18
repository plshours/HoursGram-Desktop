/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace HoursGram::Code {

// HM Code: a code editor in a window of its own, a web page in the
// built-in browser of the client. The page cannot reach the disk or the
// network itself, so it asks the client: to pick a folder and read or
// write files inside it, to copy text, and to talk to the Manul assistant
// on api.manulchiki.shop with the key the user pasted. Opening it again
// raises the window that is open.
void Show(not_null<Window::SessionController*> controller);

} // namespace HoursGram::Code
