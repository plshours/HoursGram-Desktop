/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace HoursGram::Music {

inline constexpr auto kEqualizerBands = 5;
inline constexpr auto kEqualizerMaxGain = 12.;

// The gains of the bands are in decibels, from -12 to 12.
struct EqualizerState {
	bool enabled = false;
	std::array<float64, kEqualizerBands> gains = {};
};

[[nodiscard]] const std::array<int, kEqualizerBands> &EqualizerFrequencies();

// The equalizer is kept in tdata/hoursgram_equalizer.json. These are
// called on the main thread.
[[nodiscard]] EqualizerState CurrentEqualizer();
void SetEqualizer(const EqualizerState &state, bool save = true);
[[nodiscard]] rpl::producer<EqualizerState> EqualizerValue();

// Filters the decoded samples of songs on the thread that decodes them:
// five peaking filters, one for every band, and a lowering of the volume
// that keeps raised bands from clipping. Takes the new settings as soon
// as they change, so moving a slider is heard at once.
class EqualizerProcessor final {
public:
	void reset();
	void process(int16 *samples, int frames, int channels, int frequency);

private:
	struct Coefficients {
		float64 b0 = 1.;
		float64 b1 = 0.;
		float64 b2 = 0.;
		float64 a1 = 0.;
		float64 a2 = 0.;
		bool active = false;
	};

	void refresh(int frequency);

	std::array<Coefficients, kEqualizerBands> _bands;
	std::array<std::array<float64, 2 * kEqualizerBands>, 2> _memory = {};
	uint64 _version = 0;
	int _frequency = 0;
	float64 _preamp = 1.;
	bool _enabled = false;

};

// The box with the switch, the presets and a slider for every band.
void EqualizerBox(not_null<Ui::GenericBox*> box);

} // namespace HoursGram::Music
