/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/music/music_equalizer.h"

#include "hoursgram/music/music_common.h"
#include "logs.h"
#include "media/audio/media_audio.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "styles/style_hoursgram_music.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <atomic>
#include <cmath>
#include <mutex>

namespace HoursGram::Music {
namespace {

constexpr auto kQuality = 1.0;
constexpr auto kPi = 3.14159265358979323846;
constexpr auto kSliderRowHeight = 40;

struct Storage {
	std::mutex mutex;
	EqualizerState state;
	std::atomic<uint64> version = 1;
	bool loaded = false;
};

[[nodiscard]] Storage &Data() {
	static auto result = Storage();
	return result;
}

[[nodiscard]] rpl::event_stream<EqualizerState> &Changes() {
	static auto result = rpl::event_stream<EqualizerState>();
	return result;
}

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + u"tdata/hoursgram_equalizer.json"_q;
}

// Called with the mutex held.
void EnsureLoaded(Storage &data) {
	if (data.loaded) {
		return;
	}
	data.loaded = true;
	auto file = QFile(FilePath());
	if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
		return;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	data.state.enabled = object.value(u"enabled"_q).toBool();
	const auto gains = object.value(u"gains"_q).toArray();
	for (auto i = 0; i != kEqualizerBands && i < gains.size(); ++i) {
		data.state.gains[i] = std::clamp(
			gains[i].toDouble(),
			-kEqualizerMaxGain,
			kEqualizerMaxGain);
	}
}

void Save(const EqualizerState &state) {
	auto gains = QJsonArray();
	for (const auto gain : state.gains) {
		gains.push_back(gain);
	}
	auto object = QJsonObject();
	object.insert(u"enabled"_q, state.enabled);
	object.insert(u"gains"_q, gains);
	auto file = QSaveFile(FilePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

struct Preset {
	const char *english;
	const char *russian;
	std::array<float64, kEqualizerBands> gains;
};

[[nodiscard]] const std::vector<Preset> &Presets() {
	static const auto result = std::vector<Preset>{
		{ "Flat", "Обычный", { 0., 0., 0., 0., 0. } },
		{ "Bass", "Басы", { 6., 4., 0., 0., 0. } },
		{ "Vocal", "Вокал", { -2., 0., 3., 3., 1. } },
		{ "Rock", "Рок", { 4., 2., -1., 2., 4. } },
		{ "Electronic", "Электроника", { 5., 2., 0., 2., 4. } },
		{ "Classical", "Классика", { 3., 1., 0., 1., 3. } },
		{ "Softer highs", "Мягкие верха", { 0., 0., 0., -3., -6. } },
	};
	return result;
}

[[nodiscard]] QString FrequencyText(int frequency) {
	return (frequency >= 1000)
		? (QString::number(frequency / 1000.) + Tr(" kHz", " кГц"))
		: (QString::number(frequency) + Tr(" Hz", " Гц"));
}

[[nodiscard]] QString GainText(float64 gain) {
	const auto rounded = int(std::round(gain));
	return ((rounded > 0) ? u"+"_q : QString())
		+ QString::number(rounded)
		+ Tr(" dB", " дБ");
}

[[nodiscard]] float64 SliderValue(float64 gain) {
	return (gain + kEqualizerMaxGain) / (2 * kEqualizerMaxGain);
}

[[nodiscard]] float64 SliderGain(float64 value) {
	return std::round(value * 2 * kEqualizerMaxGain - kEqualizerMaxGain);
}

} // namespace

const std::array<int, kEqualizerBands> &EqualizerFrequencies() {
	static const auto result = std::array<int, kEqualizerBands>{
		60,
		230,
		910,
		3600,
		14000,
	};
	return result;
}

EqualizerState CurrentEqualizer() {
	auto &data = Data();
	const auto lock = std::lock_guard(data.mutex);
	EnsureLoaded(data);
	return data.state;
}

void SetEqualizer(const EqualizerState &state, bool save) {
	auto &data = Data();
	{
		const auto lock = std::lock_guard(data.mutex);
		EnsureLoaded(data);
		data.state = state;
		++data.version;
	}
	if (save) {
		Save(state);
		if (const auto mixer = Media::Player::mixer()) {
			mixer->hoursgramRebufferSong();
		}
	}
	Changes().fire_copy(state);
}

rpl::producer<EqualizerState> EqualizerValue() {
	return rpl::single(CurrentEqualizer()) | rpl::then(Changes().events());
}

void EqualizerProcessor::reset() {
	_memory = {};
}

void EqualizerProcessor::refresh(int frequency) {
	auto &data = Data();
	auto state = EqualizerState();
	{
		const auto lock = std::lock_guard(data.mutex);
		EnsureLoaded(data);
		state = data.state;
		_version = data.version.load();
	}
	if (_enabled != state.enabled) {
		reset();
	}
	_enabled = state.enabled;
	_frequency = frequency;

	// Raised bands would push loud parts past the limit of 16 bits, so
	// the whole signal is lowered by the largest raise first.
	auto loudest = 0.;
	const auto &frequencies = EqualizerFrequencies();
	for (auto i = 0; i != kEqualizerBands; ++i) {
		const auto gain = state.gains[i];
		loudest = std::max(loudest, gain);
		auto &band = _bands[i];
		const auto center = float64(frequencies[i]);
		if (std::abs(gain) < 0.05 || center >= frequency / 2.) {
			band = Coefficients();
			continue;
		}
		// A peaking filter from the Audio EQ Cookbook by R. Bristow-Johnson.
		const auto a = std::pow(10., gain / 40.);
		const auto w0 = 2. * kPi * center / frequency;
		const auto alpha = std::sin(w0) / (2. * kQuality);
		const auto cosine = std::cos(w0);
		const auto a0 = 1. + alpha / a;
		band.b0 = (1. + alpha * a) / a0;
		band.b1 = (-2. * cosine) / a0;
		band.b2 = (1. - alpha * a) / a0;
		band.a1 = (-2. * cosine) / a0;
		band.a2 = (1. - alpha / a) / a0;
		band.active = true;
	}
	_preamp = std::pow(10., -loudest / 20.);
	LOG(("[eq] %1, %2 Hz, gains %3 %4 %5 %6 %7 dB.").arg(
		_enabled ? u"on"_q : u"off"_q).arg(
		frequency).arg(
		state.gains[0]).arg(
		state.gains[1]).arg(
		state.gains[2]).arg(
		state.gains[3]).arg(
		state.gains[4]));
}

void EqualizerProcessor::process(
		int16 *samples,
		int frames,
		int channels,
		int frequency) {
	if (!samples || frames <= 0 || channels < 1 || channels > 2) {
		return;
	}
	if (_version != Data().version.load() || _frequency != frequency) {
		refresh(frequency);
	}
	if (!_enabled) {
		return;
	}
	for (auto frame = 0; frame != frames; ++frame) {
		for (auto channel = 0; channel != channels; ++channel) {
			auto &sample = samples[frame * channels + channel];
			auto value = sample * _preamp;
			auto &memory = _memory[channel];
			for (auto i = 0; i != kEqualizerBands; ++i) {
				const auto &band = _bands[i];
				if (!band.active) {
					continue;
				}
				auto &z1 = memory[2 * i];
				auto &z2 = memory[2 * i + 1];
				const auto result = band.b0 * value + z1;
				z1 = band.b1 * value - band.a1 * result + z2;
				z2 = band.b2 * value - band.a2 * result;
				value = result;
			}
			sample = int16(std::clamp(value, -32768., 32767.));
		}
	}
}

void EqualizerBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(Tr("Equalizer", "Эквалайзер")));
	box->setWidth(st::boxWideWidth);

	struct State {
		EqualizerState value;
		std::vector<Fn<void()>> refreshers;
	};
	const auto state = box->lifetime().make_state<State>();
	state->value = CurrentEqualizer();
	const auto apply = [=](bool save) {
		SetEqualizer(state->value, save);
		for (const auto &refresh : state->refreshers) {
			refresh();
		}
	};

	const auto content = box->verticalLayout();
	const auto toggle = content->add(object_ptr<Ui::SettingsButton>(
		content,
		rpl::single(Tr("Equalizer for music", "Эквалайзер для музыки")),
		st::settingsButtonNoIcon));
	toggle->toggleOn(rpl::single(state->value.enabled));
	toggle->toggledChanges() | rpl::on_next([=](bool enabled) {
		state->value.enabled = enabled;
		apply(true);
	}, toggle->lifetime());

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Tr("Presets", "Пресеты")));

	// The presets wrap onto as many lines as the width of the box needs.
	const auto presets = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::boxRowPadding);
	auto buttons = std::vector<not_null<Ui::RoundButton*>>();
	for (const auto &preset : Presets()) {
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			presets,
			rpl::single(Tr(preset.english, preset.russian)),
			st::hoursgramMusicActionSecondary);
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		const auto gains = preset.gains;
		button->setClickedCallback([=] {
			state->value.gains = gains;
			state->value.enabled = true;
			toggle->toggleOn(rpl::single(true));
			apply(true);
		});
		button->show();
		buttons.push_back(button);
	}
	presets->widthValue() | rpl::on_next([=](int width) {
		const auto skip = st::hoursgramMusicActionSkip;
		auto x = 0;
		auto y = 0;
		auto lineHeight = 0;
		for (const auto &button : buttons) {
			if (x > 0 && x + button->width() > width) {
				x = 0;
				y += lineHeight + skip;
			}
			button->moveToLeft(x, y, width);
			x += button->width() + skip;
			lineHeight = std::max(lineHeight, button->height());
		}
		presets->resize(width, y + lineHeight);
	}, presets->lifetime());

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Tr("Bands", "Полосы")));

	const auto &frequencies = EqualizerFrequencies();
	for (auto i = 0; i != kEqualizerBands; ++i) {
		const auto row = content->add(
			object_ptr<Ui::FixedHeightWidget>(content, kSliderRowHeight),
			st::boxRowPadding);
		const auto name = Ui::CreateChild<Ui::FlatLabel>(
			row,
			FrequencyText(frequencies[i]),
			st::defaultFlatLabel);
		const auto value = Ui::CreateChild<Ui::FlatLabel>(
			row,
			GainText(state->value.gains[i]),
			st::defaultFlatLabel);
		const auto slider = Ui::CreateChild<Ui::MediaSlider>(
			row,
			st::hoursgramPlayerSlider);
		slider->setAlwaysDisplayMarker(true);
		slider->setValue(SliderValue(state->value.gains[i]));
		slider->setChangeProgressCallback([=](float64 progress) {
			state->value.gains[i] = SliderGain(progress);
			value->setText(GainText(state->value.gains[i]));
			if (!state->value.enabled) {
				state->value.enabled = true;
				toggle->toggleOn(rpl::single(true));
			}
			SetEqualizer(state->value, false);
		});
		slider->setChangeFinishedCallback([=](float64 progress) {
			state->value.gains[i] = SliderGain(progress);
			apply(true);
		});
		state->refreshers.push_back([=] {
			slider->setValue(SliderValue(state->value.gains[i]));
			value->setText(GainText(state->value.gains[i]));
		});
		name->show();
		value->show();
		slider->show();
		row->widthValue() | rpl::on_next([=](int width) {
			const auto labelWidth = st::hoursgramMusicBarTimeWidth + 24;
			name->moveToLeft(0, (kSliderRowHeight - name->height()) / 2, width);
			value->moveToRight(0, (kSliderRowHeight - value->height()) / 2, width);
			slider->setGeometry(
				labelWidth,
				(kSliderRowHeight - st::hoursgramPlayerSliderHeight) / 2,
				std::max(width - 2 * labelWidth, 1),
				st::hoursgramPlayerSliderHeight);
		}, row->lifetime());
	}

	box->addButton(rpl::single(Tr("Done", "Готово")), [=] {
		box->closeBox();
	});
}

} // namespace HoursGram::Music
