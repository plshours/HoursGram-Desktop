/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/plugins/plugins_box.h"

#include "core/application.h"
#include "core/file_utilities.h"
#include "hoursgram/plugins/plugins.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>

namespace HoursGram::Plugins {
namespace {

// Like the rest of the HorusGram pages, these texts carry their own
// Russian, since the client has no translations of its keys.
[[nodiscard]] bool UsesRussian() {
	const auto &lang = Lang::GetInstance();
	return lang.id().startsWith(u"ru"_q) || lang.baseId().startsWith(u"ru"_q);
}

[[nodiscard]] QString L(const QString &english, const QString &russian) {
	return UsesRussian() ? russian : english;
}

// A setting is kept as JSON text, the way HG.settings.get reads it.
[[nodiscard]] QJsonValue DecodeValue(const QString &json) {
	if (json.isEmpty()) {
		return QJsonValue(QJsonValue::Undefined);
	}
	const auto array = QJsonDocument::fromJson(
		(u"["_q + json + u"]"_q).toUtf8()).array();
	return array.isEmpty()
		? QJsonValue(QJsonValue::Undefined)
		: array.at(0);
}

[[nodiscard]] QString EncodeValue(const QJsonValue &value) {
	const auto text = QString::fromUtf8(QJsonDocument(
		QJsonArray{ value }).toJson(QJsonDocument::Compact));
	return text.mid(1, text.size() - 2);
}

[[nodiscard]] QJsonValue CurrentValue(
		not_null<Plugin*> plugin,
		const QJsonObject &entry) {
	const auto key = entry.value(u"key"_q).toString();
	const auto stored = DecodeValue(Manager::Instance().setting(plugin, key));
	return stored.isUndefined() ? entry.value(u"default"_q) : stored;
}

[[nodiscard]] QString Describe(not_null<Plugin*> plugin) {
	auto lines = QStringList();
	if (!plugin->author.isEmpty()) {
		lines.push_back(L(u"by "_q, u"автор: "_q) + plugin->author);
	}
	if (!plugin->description.isEmpty()) {
		lines.push_back(plugin->description);
	}
	if (!plugin->commands.empty()) {
		auto names = QStringList();
		for (const auto &command : plugin->commands) {
			names.push_back(u"."_q + command.name);
		}
		lines.push_back(
			L(u"Commands: "_q, u"Команды: "_q) + names.join(u", "_q));
	}
	if (!plugin->error.isEmpty()) {
		lines.push_back(
			L(u"Failed to start: "_q, u"Не запустился: "_q)
			+ plugin->error.split('\n').front());
	}
	return lines.join('\n');
}

void AddLabel(
		not_null<Ui::VerticalLayout*> container,
		const QString &text) {
	if (text.isEmpty()) {
		return;
	}
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(text),
			st::boxDividerLabel),
		st::boxRowPadding);
}

void InstallFromFile(not_null<Window::SessionController*> controller) {
	FileDialog::GetOpenPath(
		Core::App().getFileDialogParent(),
		L(u"Install a plugin"_q, u"Установить плагин"_q),
		L(
			u"HorusGram plugins (*.hgplugin);;All files (*)"_q,
			u"Плагины HorusGram (*.hgplugin);;Все файлы (*)"_q),
		crl::guard(controller, [=](FileDialog::OpenResult &&result) {
			if (!result.paths.isEmpty()) {
				if (!AskInstall(controller, result.paths.front())) {
					controller->show(Ui::MakeInformBox(L(
						u"This file is not a HorusGram plugin."_q,
						u"Этот файл не плагин HorusGram."_q)));
				}
			}
		}));
}

void ShowInstalled(
		not_null<Window::SessionController*> controller,
		Plugin *plugin) {
	if (!plugin) {
		controller->showToast(L(
			u"This file is not a HorusGram plugin."_q,
			u"Этот файл не плагин HorusGram."_q));
	} else if (!plugin->error.isEmpty()) {
		controller->showToast(L(
			u"The plugin is installed but failed to start: "_q,
			u"Плагин установлен, но не запустился: "_q)
			+ plugin->error.split('\n').front());
	} else {
		controller->showToast(L(
			u"Plugin installed: "_q,
			u"Плагин установлен: "_q) + plugin->name);
	}
}

} // namespace

void ShowPluginsBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(L(u"Plugins"_q, u"Плагины"_q)));
		box->setWidth(st::boxWideWidth);

		const auto container = box->verticalLayout();
		const auto inner = container->add(
			object_ptr<Ui::VerticalLayout>(container));

		const auto rebuild = [=] {
			inner->clear();
			auto &manager = Manager::Instance();
			const auto &list = manager.list();
			if (list.empty()) {
				Ui::AddSkip(inner);
				AddLabel(inner, L(
					u"No plugins yet. A plugin is a .hgplugin file: the same"
					" file works in HorusGram on the PC and on Android. Send"
					" one to any chat and open it, or install it from a file"
					" below."_q,
					u"Плагинов пока нет. Плагин — это файл .hgplugin: один и"
					" тот же файл работает в HorusGram на ПК и на Android."
					" Отправьте его в любой чат и откройте или установите из"
					" файла кнопкой ниже."_q));
				Ui::AddSkip(inner);
				const auto example = inner->add(object_ptr<Ui::SettingsButton>(
					inner,
					rpl::single(L(
						u"Install the example plugin"_q,
						u"Установить пример плагина"_q)),
					st::settingsButtonNoIcon));
				example->setClickedCallback([=] {
					ShowInstalled(
						controller,
						Manager::Instance().installExample());
				});
				inner->resizeToWidth(st::boxWideWidth);
				return;
			}
			for (const auto &entry : list) {
				const auto plugin = entry.get();
				const auto id = plugin->id;
				Ui::AddSkip(inner);
				auto title = plugin->name;
				if (!plugin->version.isEmpty()) {
					title += ' ' + plugin->version;
				}
				const auto toggle = inner->add(object_ptr<Ui::SettingsButton>(
					inner,
					rpl::single(title),
					st::settingsButtonNoIcon));
				toggle->toggleOn(rpl::single(plugin->enabled));
				toggle->toggledChanges(
				) | rpl::on_next([=](bool enabled) {
					// Rebuilding here would delete the button under us.
					crl::on_main(toggle, [=] {
						if (const auto found = Manager::Instance().find(id)) {
							Manager::Instance().setEnabled(found, enabled);
						}
					});
				}, toggle->lifetime());
				AddLabel(inner, Describe(plugin));
				if (plugin->running() && !plugin->settings.isEmpty()) {
					const auto settings = inner->add(
						object_ptr<Ui::SettingsButton>(
							inner,
							rpl::single(L(u"Settings"_q, u"Настройки"_q)),
							st::settingsButtonNoIcon));
					settings->setClickedCallback([=] {
						ShowPluginSettingsBox(controller, id);
					});
				}
				const auto remove = inner->add(object_ptr<Ui::SettingsButton>(
					inner,
					rpl::single(L(u"Delete"_q, u"Удалить"_q)),
					st::settingsAttentionButton));
				const auto name = plugin->name;
				remove->setClickedCallback([=] {
					controller->show(Ui::MakeConfirmBox({
						.text = L(
							u"Delete the plugin %1 with its settings?"_q,
							u"Удалить плагин %1 вместе с его настройками?"_q
						).arg(name),
						.confirmed = [=](Fn<void()> &&close) {
							close();
							if (const auto found = Manager::Instance().find(id)) {
								Manager::Instance().remove(found);
							}
						},
						.confirmText = L(u"Delete"_q, u"Удалить"_q),
						.confirmStyle = &st::attentionBoxButton,
					}));
				});
				Ui::AddSkip(inner);
				Ui::AddDivider(inner);
			}
			inner->resizeToWidth(st::boxWideWidth);
		};
		rebuild();

		Manager::Instance().changes(
		) | rpl::on_next([=] {
			rebuild();
		}, box->lifetime());

		box->addLeftButton(
			rpl::single(L(u"Install from file"_q, u"Установить из файла"_q)),
			[=] { InstallFromFile(controller); });
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowPluginSettingsBox(
		not_null<Window::SessionController*> controller,
		const QString &id) {
	const auto plugin = Manager::Instance().find(id);
	if (!plugin) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(plugin->name));
		const auto container = box->verticalLayout();

		struct Field {
			QString key;
			Ui::InputField *field = nullptr;
		};
		const auto fields = box->lifetime().make_state<std::vector<Field>>();

		for (const auto &value : plugin->settings) {
			const auto entry = value.toObject();
			const auto type = entry.value(u"type"_q).toString();
			const auto key = entry.value(u"key"_q).toString();
			const auto title = entry.value(u"title"_q).toString();
			if (type == u"header"_q) {
				Ui::AddSkip(container);
				Ui::AddSubsectionTitle(container, rpl::single(title));
			} else if (type == u"info"_q) {
				Ui::AddDividerText(container, rpl::single(title));
			} else if (type == u"switch"_q && !key.isEmpty()) {
				const auto button = container->add(
					object_ptr<Ui::SettingsButton>(
						container,
						rpl::single(title),
						st::settingsButtonNoIcon));
				button->toggleOn(
					rpl::single(CurrentValue(plugin, entry).toBool()));
				button->toggledChanges(
				) | rpl::on_next([=](bool on) {
					if (const auto found = Manager::Instance().find(id)) {
						Manager::Instance().setSetting(
							found,
							key,
							EncodeValue(on));
					}
				}, button->lifetime());
			} else if (type == u"text"_q && !key.isEmpty()) {
				const auto current = CurrentValue(plugin, entry);
				const auto field = container->add(
					object_ptr<Ui::InputField>(
						container,
						st::defaultInputField,
						rpl::single(title),
						current.isString()
							? current.toString()
							: current.isDouble()
							? QString::number(current.toDouble())
							: QString()),
					st::boxRowPadding);
				fields->push_back({ key, field });
				Ui::AddSkip(container);
			}
		}

		box->addButton(tr::lng_settings_save(), [=] {
			if (const auto found = Manager::Instance().find(id)) {
				for (const auto &field : *fields) {
					Manager::Instance().setSetting(
						found,
						field.key,
						EncodeValue(field.field->getLastText()));
				}
			}
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

bool AskInstall(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 512 * 1024) {
		return false;
	}
	const auto source = file.readAll();
	const auto parsed = Parse(source);
	if (!parsed) {
		return false;
	}
	const auto installed = (Manager::Instance().find(parsed->id) != nullptr);

	auto text = parsed->name;
	if (!parsed->version.isEmpty()) {
		text += ' ' + parsed->version;
	}
	if (!parsed->author.isEmpty()) {
		text += '\n' + L(u"by "_q, u"автор: "_q) + parsed->author;
	}
	if (!parsed->description.isEmpty()) {
		text += u"\n\n"_q + parsed->description;
	}
	text += u"\n\n"_q + L(
		u"The plugin will see the text messages you send and receive, can"
		" change or stop the ones you send, send messages itself and keep"
		" its own data. It has no access to your files or the network."_q,
		u"Плагин будет видеть текстовые сообщения, которые вы отправляете и"
		" получаете, сможет менять или отменять отправляемые, сам отправлять"
		" сообщения и хранить свои данные. Доступа к файлам и сети у него"
		" нет."_q);

	const auto action = installed
		? L(u"Update"_q, u"Обновить"_q)
		: L(u"Install"_q, u"Установить"_q);
	controller->show(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = [=](Fn<void()> &&close) {
			close();
			ShowInstalled(controller, Manager::Instance().install(source));
		},
		.confirmText = action,
		.title = installed
			? L(u"Update plugin"_q, u"Обновить плагин"_q)
			: L(u"Install plugin"_q, u"Установить плагин"_q),
	}));
	return true;
}

} // namespace HoursGram::Plugins
