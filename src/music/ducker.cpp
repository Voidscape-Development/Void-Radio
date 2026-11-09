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

#include "music/ducker.hpp"

#include <algorithm>
#include <cmath>

namespace vr {

float Ducker::gain() const
{
	return std::pow(10.0f, gain_db_ / 20.0f);
}

float Ducker::process(float trigger_peak, size_t frames, uint32_t sample_rate)
{
	const float block_ms = sample_rate ? 1000.0f * (float)frames / (float)sample_rate : 0.0f;
	const float depth = std::max(0.0f, settings_.depth_db);

	float target = 0.0f;

	if (settings_.enabled) {
		const float level_db = trigger_peak > 0.0f ? 20.0f * std::log10(trigger_peak) : -200.0f;

		if (level_db >= settings_.threshold_db)
			hold_left_ms_ = (float)std::max(0, settings_.hold_ms);
		else
			hold_left_ms_ = std::max(0.0f, hold_left_ms_ - block_ms);

		if (hold_left_ms_ > 0.0f)
			target = -depth;
	} else {
		hold_left_ms_ = 0.0f;
	}

	/* The whole depth in `attack_ms` going down and `release_ms` coming
	 * back; a depth of zero still comes back from wherever it was. */
	const float span = std::max(depth, 1.0f);

	if (target < gain_db_) {
		const float rate = span / (float)std::max(1, settings_.attack_ms);
		gain_db_ = std::max(target, gain_db_ - rate * block_ms);
	} else if (target > gain_db_) {
		const float rate = span / (float)std::max(1, settings_.release_ms);
		gain_db_ = std::min(target, gain_db_ + rate * block_ms);
	}

	return gain();
}

} // namespace vr
