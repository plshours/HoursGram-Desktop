/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/plugins/plugins_box.h"

#include "core/file_utilities.h"
#include "hoursgram/plugins/plugins.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace HoursGram::Plugins {
namespace {

// The value a setting has now: what the user picked, or the plugin's default.
[[nodiscard]] QJsonValue SettingValue(
		not_null<Plugin*> plugin,
		const QJsonObject &entry) {
	const auto json = SettingJson(plugin, entry.value(u"key"_q).toString());
	if (!json.isEmpty()) {
		const auto parsed = QJsonDocument::fromJson(
			(u"["_q + json + u"]"_q).toUtf8());
		if (parsed.isArray() && !parsed.array().isEmpty()) {
			return parsed.array().first();
		}
	}
	return entry.value(u"default"_q);
}

[[nodiscard]] QString JsonText(const QString &text) {
	const auto array = QJsonDocument(QJsonArray{ text }).toJson(
		QJsonDocument::Compact);
	// [ "..." ] without the brackets.
	return QString::fromUtf8(array.mid(1, array.size() - 2));
}

[[nodiscard]] QString ValueText(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString();
	} else if (value.isDouble()) {
		return QString::number(value.toDouble());
	} else if (value.isBool()) {
		return value.toBool() ? u"true"_q : u"false"_q;
	}
	return QString();
}

void PromptText(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &initial,
		Fn<void(QString)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			rpl::single(title),
			initial));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto save = [=] {
			done(field->getLastText());
			box->closeBox();
		};
		field->submits() | rpl::on_next([=] {
			save();
		}, field->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowSettingsBox(
		not_null<Window::SessionController*> controller,
		not_null<Plugin*> plugin) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(plugin->name));
		box->setWidth(st::boxWideWidth);
		const auto container = box->verticalLayout();
		const auto schema = QJsonDocument::fromJson(
			plugin->settingsJson.toUtf8()).array();
		for (const auto &value : schema) {
			const auto entry = value.toObject();
			const auto type = entry.value(u"type"_q).toString();
			const auto title = entry.value(u"title"_q).toString();
			const auto key = entry.value(u"key"_q).toString();
			if (type == u"header"_q) {
				Ui::AddSkip(container);
				Ui::AddSubsectionTitle(container, rpl::single(title));
			} else if (type == u"switch"_q && !key.isEmpty()) {
				const auto button = Settings::AddButtonWithIcon(
					container,
					rpl::single(title),
					st::settingsButtonNoIcon);
				button->toggleOn(
					rpl::single(SettingValue(plugin, entry).toBool())
				)->toggledValue(
				) | rpl::skip(1) | rpl::on_next([=](bool enabled) {
					SetSettingJson(
						plugin,
						key,
						enabled ? u"true"_q : u"false"_q);
				}, button->lifetime());
			} else if (type == u"text"_q && !key.isEmpty()) {
				const auto label = box->lifetime().make_state<
					rpl::variable<QString>>(
						ValueText(SettingValue(plugin, entry)));
				const auto button = Settings::AddButtonWithLabel(
					container,
					rpl::single(title),
					label->value(),
					st::settingsButtonNoIcon);
				button->setClickedCallback([=] {
					PromptText(controller, title, label->current(), [=](
							QString text) {
						SetSettingJson(plugin, key, JsonText(text));
						*label = text;
					});
				});
			} else if (type == u"info"_q) {
				Ui::AddDividerText(container, rpl::single(title));
			}
		}
		Ui::AddSkip(container);
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

[[nodiscard]] QString About(not_null<Plugin*> plugin) {
	auto lines = QStringList();
	if (!plugin->description.isEmpty()) {
		lines.push_back(plugin->description);
	}
	if (!plugin->author.isEmpty()) {
		lines.push_back(tr::hoursgram_PluginAuthor(
			tr::now,
			lt_name,
			plugin->author));
	}
	if (!plugin->commands.empty()) {
		lines.push_back(tr::hoursgram_PluginCommands(tr::now));
		for (const auto &command : plugin->commands) {
			lines.push_back(command.description.isEmpty()
				? (u"."_q + command.name)
				: (u"."_q + command.name + u" - "_q + command.description));
		}
	}
	if (!plugin->error.isEmpty()) {
		lines.push_back(tr::hoursgram_PluginFailed(
			tr::now,
			lt_error,
			plugin->error.split('\n').front()));
	}
	return lines.join('\n');
}

void Report(
		not_null<Window::SessionController*> controller,
		Plugin *plugin) {
	if (!plugin) {
		controller->showToast(tr::hoursgram_PluginInvalid(tr::now));
	} else if (!plugin->error.isEmpty()) {
		controller->showToast(tr::hoursgram_PluginFailed(
			tr::now,
			lt_error,
			plugin->error.split('\n').front()));
	} else {
		controller->showToast(tr::hoursgram_PluginInstalled(
			tr::now,
			lt_name,
			plugin->name));
	}
}

} // namespace

void ShowPluginsBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::hoursgram_Plugins());
		box->setWidth(st::boxWideWidth);
		const auto container = box->verticalLayout();
		const auto inner = container->add(
			object_ptr<Ui::VerticalLayout>(container));

		const auto rebuild = [=] {
			inner->clear();
			const auto list = List();
			if (list.empty()) {
				Ui::AddSkip(inner);
				Ui::AddDividerText(inner, tr::hoursgram_PluginsEmpty());
			}
			for (const auto plugin : list) {
				Ui::AddSkip(inner);
				Ui::AddSubsectionTitle(
					inner,
					rpl::single(plugin->version.isEmpty()
						? plugin->name
						: (plugin->name + u" "_q + plugin->version)));
				const auto toggle = Settings::AddButtonWithIcon(
					inner,
					tr::hoursgram_PluginEnabled(),
					st::settingsButtonNoIcon);
				toggle->toggleOn(
					rpl::single(plugin->enabled)
				)->toggledValue(
				) | rpl::skip(1) | rpl::on_next([=](bool enabled) {
					// The list is built again after this, so not now.
					crl::on_main(box, [=] {
						SetEnabled(plugin, enabled);
					});
				}, toggle->lifetime());
				if (plugin->enabled && plugin->hasSettings()) {
					Settings::AddButtonWithIcon(
						inner,
						tr::hoursgram_PluginSettings(),
						st::settingsButtonNoIcon
					)->setClickedCallback([=] {
						ShowSettingsBox(controller, plugin);
					});
				}
				Settings::AddButtonWithIcon(
					inner,
					tr::hoursgram_PluginDelete(),
					st::settingsAttentionButton
				)->setClickedCallback([=] {
					controller->show(Ui::MakeConfirmBox({
						.text = tr::hoursgram_PluginDeleteSure(
							tr::now,
							lt_name,
							plugin->name),
						.confirmed = [=](Fn<void()> close) {
							close();
							Remove(plugin);
						},
						.confirmText = tr::lng_box_delete(tr::now),
					}));
				});
				const auto about = About(plugin);
				if (!about.isEmpty()) {
					Ui::AddDividerText(inner, rpl::single(about));
				}
			}
			inner->resizeToWidth(container->width());
		};
		rebuild();
		Changes() | rpl::on_next([=] {
			rebuild();
		}, inner->lifetime());

		Ui::AddSkip(container);
		Settings::AddButtonWithIcon(
			container,
			tr::hoursgram_PluginFromFile(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			const auto chosen = crl::guard(box, [=](
					FileDialog::OpenResult &&result) {
				auto bytes = result.remoteContent;
				if (bytes.isEmpty() && !result.paths.isEmpty()) {
					auto file = QFile(result.paths.front());
					if (file.open(QIODevice::ReadOnly)) {
						bytes = file.readAll();
					}
				}
				Report(controller, Install(QString::fromUtf8(bytes)));
			});
			FileDialog::GetOpenPath(
				box,
				tr::hoursgram_PluginFromFile(tr::now),
				tr::hoursgram_PluginFilter(tr::now)
					+ u";;"_q
					+ FileDialog::AllFilesFilter(),
				chosen);
		});
		Settings::AddButtonWithIcon(
			container,
			tr::hoursgram_PluginExample(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			Report(controller, InstallExample());
		});
		Ui::AddDividerText(container, tr::hoursgram_PluginsAbout());

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace HoursGram::Plugins
