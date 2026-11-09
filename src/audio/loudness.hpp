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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vr {

/*
 * Integrated loudness, as ITU-R BS.1770 and EBU R128 define it: the audio is
 * K-weighted (a high shelf and a high pass, roughly how loud the ear hears
 * each frequency), cut into 400 ms blocks that overlap by three quarters, and
 * averaged over the blocks that are not near silence. The result is in LUFS;
 * a full scale 1 kHz sine on both channels of a stereo file reads 0.
 *
 * Also keeps the sample peak. Pure arithmetic, tested on its own.
 */
class LoudnessMeter {
public:
	LoudnessMeter(uint32_t sample_rate, uint32_t channels);

	/* Planar float samples, one buffer per channel. */
	void add(const float *const *planes, size_t frames);

	/* NAN when nothing above the absolute gate was heard. */
	double integrated() const;

	/* The largest absolute sample, linear. */
	double peak() const { return peak_; }

private:
	struct Biquad {
		double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
		double z1 = 0, z2 = 0;

		double run(double x)
		{
			const double y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
	};

	void finish_step();

	uint32_t rate_;
	uint32_t channels_;
	std::vector<Biquad> shelf_;
	std::vector<Biquad> highpass_;

	/* Energy of each 100 ms step, and the last four of them that make up
	 * the current 400 ms block. */
	size_t step_frames_;
	size_t step_filled_ = 0;
	double step_energy_ = 0.0;
	double recent_[4] = {};
	size_t steps_ = 0;

	std::vector<double> blocks_; /* mean square energy of each 400 ms block */
	double peak_ = 0.0;
};

} // namespace vr
