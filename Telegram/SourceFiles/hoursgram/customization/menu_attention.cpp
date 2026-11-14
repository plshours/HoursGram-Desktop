/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/menu_attention.h"

#include "ui/widgets/menu/menu.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_menu_icons.h"

namespace HoursGram::Customization {

void StartMenuAttention() {
	Ui::Menu::SetHoursGramAttention([](const style::icon *icon)
	-> const style::icon* {
		if (icon == &st::menuIconDelete) {
			return &st::menuIconDeleteAttention;
		} else if (icon == &st::menuIconLeave) {
			return &st::menuIconLeaveAttention;
		} else if (icon == &st::menuIconBlock) {
			return &st::menuIconBlockAttention;
		} else if (icon == &st::menuIconReport) {
			return &st::menuIconReportAttention;
		} else if (icon == &st::menuIconRemove) {
			return &st::menuIconRemoveAttention;
		} else if (icon == &st::menuIconClear) {
			return &st::menuIconClearAttention;
		}
		return nullptr;
	});
}

} // namespace HoursGram::Customization
