// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_local_nft.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "boxes/star_gift_box.h"
#include "data/data_user.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;

namespace {

void ShowLocalProfileTextBox(
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		rpl::producer<QString> placeholder,
		QString current,
		Fn<void(QString)> saveValue) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(title);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::NoNewlines,
			placeholder,
			current));
		field->setMaxLength(32);
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::hoursgram_LocalOnlyNotice(),
			st::boxDividerLabel));
		const auto submit = [=] {
			saveValue(field->getLastText().trimmed());
			box->closeBox();
		};
		field->submits() | rpl::on_next([=](auto) {
			submit();
		}, field->lifetime());
		box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowLocalNftNumberBox(
		not_null<Window::SessionController*> controller,
		Fn<void(int)> saveNumber) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::hoursgram_LocalNftNumber());
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::NoNewlines,
			tr::hoursgram_LocalNftNumber(),
			u"1"_q));
		field->setMaxLength(10);
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::hoursgram_LocalOnlyNotice(),
			st::boxDividerLabel));
		const auto submit = [=] {
			auto valid = false;
			const auto number = field->getLastText().trimmed().toInt(&valid);
			if (!valid || number <= 0) {
				field->showError();
				return;
			}
			saveNumber(number);
			box->closeBox();
		};
		field->submits() | rpl::on_next([=](auto) {
			submit();
		}, field->lifetime());
		box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

// The gift picker is the real Telegram catalogue: picking one stores its id
// locally, so what shows up on the profile is a gift that actually exists
// rather than something invented here.
void ShowGiftPicker(
		not_null<Window::SessionController*> controller,
		uint64 userId) {
	auto *settings = &AyuSettings::getInstance();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto peer = controller->session().user();
		box->setTitle(tr::hoursgram_LocalGiftMarket());
		box->setWidth(st::boxWideWidth);
		box->setMaxHeight(st::boxWideWidth * 2);
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::hoursgram_LocalGiftPickerHint(),
			st::boxDividerLabel));

		auto gifts = Info::PeerGifts::GiftsStars(
			&controller->session(),
			peer
		) | rpl::map([](
				std::vector<Info::PeerGifts::GiftTypeStars> entries) {
			Ui::GiftsDescriptor result;
			for (const auto &entry : entries) {
				if (!entry.resale) {
					result.list.push_back(entry);
				}
			}
			return result;
		});
		box->addRow(Ui::MakeGiftsList({
			.window = controller,
			.mode = Ui::GiftsListMode::Local,
			.peer = peer,
			.gifts = std::move(gifts),
			.handler = [=](Info::PeerGifts::GiftDescriptor descriptor) {
				const auto gift = std::get_if<Info::PeerGifts::GiftTypeStars>(
					&descriptor);
				if (!gift) {
					return;
				}
				const auto baseId = QString::number(gift->info.id);
				const auto oldId = u"telegram:"_q + baseId;
				if (settings->removeLocalGift(userId, oldId)) {
					controller->showToast(
						tr::hoursgram_GiftRemoved(tr::now));
					return;
				}
				const auto prefix = u"nft:"_q + baseId + ':';
				for (const auto &localGift : settings->localOwnedGifts(userId)) {
					if (localGift.startsWith(prefix)) {
						if (settings->removeLocalGift(userId, localGift)) {
							controller->showToast(
								tr::hoursgram_GiftRemoved(tr::now));
						}
						return;
					}
				}
				ShowLocalNftNumberBox(controller, [=](int number) {
					const auto id = prefix + QString::number(number);
					if (settings->addLocalGift(userId, id)) {
						controller->showToast(
							tr::hoursgram_GiftAdded(tr::now));
					}
				});
			},
			.localSelection = [=](uint64 giftId) {
				const auto id = QString::number(giftId);
				const auto prefix = u"nft:"_q + id + ':';
				for (const auto &localGift : settings->localOwnedGifts(userId)) {
					if (localGift == u"telegram:"_q + id
						|| localGift.startsWith(prefix)) {
						return true;
					}
				}
				return false;
			},
		}), QMargins());
	}));
}

void BuildGifts(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto userId = controller->session().userId().bare;
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle(tr::hoursgram_LocalGiftMarket());
	builder.addButton({
		.id = u"hoursgram/browseTelegramGifts"_q,
		.title = tr::hoursgram_BrowseTelegramGifts(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->localOwnedGiftsValue(userId) | rpl::map(
			[](const QStringList &gifts) {
				return gifts.isEmpty()
					? QString()
					: QString::number(gifts.size());
			}),
		.onClick = [=] { ShowGiftPicker(controller, userId); },
	});
	builder.addButton({
		.id = u"hoursgram/clearLocalGifts"_q,
		.title = tr::hoursgram_LocalGiftsClear(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			auto removed = false;
			for (const auto &gift : settings->localOwnedGifts(userId)) {
				if (settings->removeLocalGift(userId, gift)) {
					removed = true;
				}
			}
			if (removed) {
				controller->showToast(tr::hoursgram_GiftRemoved(tr::now));
			}
		},
	});
	builder.addDividerText(tr::hoursgram_LocalGiftPickerHint());
}

void BuildIdentity(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto userId = controller->session().userId().bare;
	auto *settings = &AyuSettings::getInstance();

	builder.addSkip();
	builder.addSubsectionTitle(tr::hoursgram_LocalProfileIdentity());
	builder.addButton({
		.id = u"hoursgram/localNftUsername"_q,
		.title = tr::hoursgram_LocalNftUsername(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->getLocalProfileUsernameValue(userId) | rpl::map(
			[](const QString &username) {
				return username.isEmpty() ? QString() : '@' + username;
			}),
		.onClick = [=] {
			ShowLocalProfileTextBox(
				controller,
				tr::hoursgram_LocalNftUsername(),
				tr::hoursgram_LocalNftUsername(),
				settings->getLocalProfileUsername(userId),
				[=](QString value) {
					settings->setLocalProfileUsername(userId, value);
				});
		},
	});
	builder.addButton({
		.id = u"hoursgram/localProfilePhone"_q,
		.title = tr::hoursgram_LocalProfilePhone(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->getLocalProfilePhoneValue(userId),
		.onClick = [=] {
			ShowLocalProfileTextBox(
				controller,
				tr::hoursgram_LocalProfilePhone(),
				tr::hoursgram_LocalProfilePhone(),
				settings->getLocalProfilePhone(userId),
				[=](QString value) {
					settings->setLocalProfilePhone(userId, value);
				});
		},
	});
	builder.addDividerText(tr::hoursgram_LocalOnlyNotice());
}

const auto kMeta = BuildHelper({
	.id = AyuLocalNft::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::hoursgram_CategoryLocalNft,
	.icon = &st::menuIconGiftPremium,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	BuildGifts(builder);
	BuildIdentity(builder);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuLocalNft::title() {
	return tr::hoursgram_CategoryLocalNft();
}

AyuLocalNft::AyuLocalNft(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuLocalNft::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuLocalNftId() {
	return AyuLocalNft::Id();
}

} // namespace Settings
