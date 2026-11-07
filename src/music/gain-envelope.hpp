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

namespace vr {

/*
 * A gain that can ramp to a new level over a number of samples. Fades are
 * linear, except crossfades, which use an equal-power sin/cos pair so the
 * combined loudness does not dip in the middle.
 */
class GainEnvelope {
public:
	/* Jumps straight to `gain`. */
	void set(double gain);

	/* Ramps from wherever the envelope is now to `target`. */
	void fade_to(double target, uint64_t samples, bool equal_power);

	double value() const;
	double target() const { return to_; }
	bool fading() const { return length_ && position_ < length_; }

	/* Settled at zero: nothing this envelope touches can be heard. */
	bool silent() const { return !fading() && to_ <= 0.0; }

	/* Scales `frames` samples of each channel in place and advances. */
	void apply(float *const *buffers, size_t channels, size_t frames);

	/* Moves along without touching any audio, so a fade keeps time while
	 * its deck has nothing to give. */
	void advance(size_t frames);

private:
	double at(uint64_t position) const;

	double from_ = 1.0;
	double to_ = 1.0;
	uint64_t position_ = 0;
	uint64_t length_ = 0;
	bool equal_power_ = false;
};

} // namespace vr
