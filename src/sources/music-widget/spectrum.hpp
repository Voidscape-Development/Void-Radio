/*
Void Radio - music playback sources for OBS Studio
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vr::widget {

/*
 * The visualiser's analysis: turns the last few thousand samples of audio into
 * one level per band, spaced logarithmically the way hearing is, or into a
 * short waveform. Pure arithmetic with no OBS in it, so it is tested on its own.
 */

/* Samples analysed per frame: about 43 ms at 48 kHz, 23 Hz per FFT bin. */
constexpr size_t SPECTRUM_WINDOW = 2048;

class Spectrum {
public:
	/*
	 * `samples` holds `count` mono samples, oldest first; only the newest
	 * SPECTRUM_WINDOW are used. Writes `bands` levels from 0 (silence, or
	 * 60 dB below full scale) to 1 (a full scale sine) into `out`.
	 * `gain` scales the signal first, 1 = as is.
	 */
	void analyse(const float *samples, size_t count, uint32_t sample_rate, int bands, float min_freq,
		     float max_freq, float gain, std::vector<float> &out);

private:
	std::vector<float> window_;
	std::vector<std::complex<float>> buffer_;
};

/* Reduces the newest samples to `points` values from -1 to 1, each the
 * largest swing within its slice, so a waveform keeps its peaks. */
void wave_points(const float *samples, size_t count, int points, float gain, std::vector<float> &out);

/*
 * Moves what is shown towards the newly measured levels: up quickly, so a
 * beat lands on time, and down at a rate set by `smoothing` (0..1, higher is
 * slower). `shown` is resized to match `target`.
 */
void smooth_levels(std::vector<float> &shown, const std::vector<float> &target, float seconds, float smoothing);

} // namespace vr::widget
