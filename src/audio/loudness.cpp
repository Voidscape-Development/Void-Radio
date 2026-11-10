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

#include "audio/loudness.hpp"

#include <algorithm>
#include <cmath>

namespace vr {

namespace {

constexpr double PI_D = 3.14159265358979323846;
constexpr double ABSOLUTE_GATE = -70.0; /* LUFS */
constexpr double RELATIVE_GATE = -10.0; /* LU below the ungated mean */

double to_lufs(double energy)
{
	return -0.691 + 10.0 * std::log10(energy);
}

} // namespace

LoudnessMeter::LoudnessMeter(uint32_t sample_rate, uint32_t channels)
	: rate_(std::max<uint32_t>(sample_rate, 8000)),
	  channels_(std::max<uint32_t>(channels, 1)),
	  shelf_(channels_),
	  highpass_(channels_),
	  step_frames_(std::max<size_t>(1, rate_ / 10))
{
	/* The filter coefficients for any sample rate, worked out from the
	 * analogue prototypes the standard's 48 kHz coefficients came from. */
	{
		const double f0 = 1681.974450955533;
		const double gain_db = 3.999843853973347;
		const double q = 0.7071752369554196;

		const double k = std::tan(PI_D * f0 / rate_);
		const double vh = std::pow(10.0, gain_db / 20.0);
		const double vb = std::pow(vh, 0.4996667741545416);
		const double a0 = 1.0 + k / q + k * k;

		Biquad shelf;
		shelf.b0 = (vh + vb * k / q + k * k) / a0;
		shelf.b1 = 2.0 * (k * k - vh) / a0;
		shelf.b2 = (vh - vb * k / q + k * k) / a0;
		shelf.a1 = 2.0 * (k * k - 1.0) / a0;
		shelf.a2 = (1.0 - k / q + k * k) / a0;
		std::fill(shelf_.begin(), shelf_.end(), shelf);
	}

	{
		const double f0 = 38.13547087602444;
		const double q = 0.5003270373238773;

		const double k = std::tan(PI_D * f0 / rate_);
		const double a0 = 1.0 + k / q + k * k;

		Biquad highpass;
		highpass.b0 = 1.0;
		highpass.b1 = -2.0;
		highpass.b2 = 1.0;
		highpass.a1 = 2.0 * (k * k - 1.0) / a0;
		highpass.a2 = (1.0 - k / q + k * k) / a0;
		std::fill(highpass_.begin(), highpass_.end(), highpass);
	}
}

void LoudnessMeter::add(const float *const *planes, size_t frames)
{
	size_t done = 0;

	while (done < frames) {
		const size_t take = std::min(frames - done, step_frames_ - step_filled_);

		for (uint32_t ch = 0; ch < channels_; ch++) {
			const float *in = planes[ch];
			if (!in)
				continue;

			Biquad &shelf = shelf_[ch];
			Biquad &highpass = highpass_[ch];
			double sum = 0.0;

			for (size_t i = done; i < done + take; i++) {
				const double x = in[i];
				peak_ = std::max(peak_, std::fabs(x));
				const double y = highpass.run(shelf.run(x));
				sum += y * y;
			}

			/* Every channel weighs the same; the standard's 1.41 for
			 * surround channels hardly matters for music in stereo. */
			step_energy_ += sum;
		}

		step_filled_ += take;
		done += take;

		if (step_filled_ == step_frames_)
			finish_step();
	}
}

void LoudnessMeter::finish_step()
{
	recent_[steps_ % 4] = step_energy_ / (double)step_frames_;
	steps_++;
	step_energy_ = 0.0;
	step_filled_ = 0;

	/* A 400 ms block every 100 ms, once there are four steps. */
	if (steps_ >= 4)
		blocks_.push_back((recent_[0] + recent_[1] + recent_[2] + recent_[3]) / 4.0);
}

double LoudnessMeter::integrated() const
{
	double sum = 0.0;
	size_t count = 0;

	for (double energy : blocks_) {
		if (energy > 0.0 && to_lufs(energy) > ABSOLUTE_GATE) {
			sum += energy;
			count++;
		}
	}

	if (!count)
		return NAN;

	const double relative = to_lufs(sum / (double)count) + RELATIVE_GATE;

	double gated = 0.0;
	size_t kept = 0;
	for (double energy : blocks_) {
		if (energy > 0.0) {
			const double lufs = to_lufs(energy);
			if (lufs > ABSOLUTE_GATE && lufs > relative) {
				gated += energy;
				kept++;
			}
		}
	}

	return kept ? to_lufs(gated / (double)kept) : NAN;
}

} // namespace vr
