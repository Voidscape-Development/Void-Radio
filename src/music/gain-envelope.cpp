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

#include "music/gain-envelope.hpp"

#include <cmath>

namespace vr {

namespace {

/* Spelled out rather than taken from <cmath>, which does not define M_PI and
 * friends on every toolchain. */
constexpr double HALF_PI = 1.57079632679489661923;

double curve(double t, bool equal_power, bool rising)
{
	if (t <= 0.0)
		return 0.0;
	if (t >= 1.0)
		return 1.0;
	if (!equal_power)
		return t;

	return rising ? std::sin(t * HALF_PI) : 1.0 - std::cos(t * HALF_PI);
}

} // namespace

void GainEnvelope::set(double gain)
{
	from_ = gain;
	to_ = gain;
	position_ = 0;
	length_ = 0;
}

double GainEnvelope::at(uint64_t position) const
{
	if (!length_ || position >= length_)
		return to_;

	const double t = (double)position / (double)length_;
	return from_ + (to_ - from_) * curve(t, equal_power_, to_ > from_);
}

double GainEnvelope::value() const
{
	return at(position_);
}

void GainEnvelope::fade_to(double target, uint64_t samples, bool equal_power)
{
	const double current = value();

	if (!samples) {
		set(target);
		return;
	}

	from_ = current;
	to_ = target;
	position_ = 0;
	length_ = samples;
	equal_power_ = equal_power;
}

void GainEnvelope::apply(float *const *buffers, size_t channels, size_t frames)
{
	if (!fading()) {
		const float gain = (float)to_;
		if (gain == 1.0f)
			return;

		for (size_t ch = 0; ch < channels; ch++)
			for (size_t i = 0; i < frames; i++)
				buffers[ch][i] *= gain;
		return;
	}

	for (size_t i = 0; i < frames; i++) {
		const float gain = (float)at(position_);
		if (position_ < length_)
			position_++;

		for (size_t ch = 0; ch < channels; ch++)
			buffers[ch][i] *= gain;
	}

	if (position_ >= length_) {
		from_ = to_;
		length_ = 0;
		position_ = 0;
	}
}

void GainEnvelope::advance(size_t frames)
{
	if (!fading())
		return;

	position_ += frames;

	if (position_ >= length_) {
		from_ = to_;
		length_ = 0;
		position_ = 0;
	}
}

} // namespace vr
