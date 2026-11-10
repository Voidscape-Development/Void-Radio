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

struct DuckSettings {
	bool enabled = false;
	float threshold_db = -35.0f; /* how loud the trigger has to be */
	float depth_db = 12.0f;      /* how far the music goes down */
	int attack_ms = 80;          /* how fast it goes down */
	int hold_ms = 500;           /* how long it stays down after the trigger goes quiet */
	int release_ms = 800;        /* how fast it comes back */
};

/*
 * Turns the music down while another source, usually a microphone, is above a
 * threshold, and back up a little after it goes quiet. Works a block at a
 * time: given the loudest the trigger was during the block, it says what gain
 * the music should have reached by the end of it. Moves in decibels at a
 * steady rate, which is what makes it sound like a hand on a fader.
 *
 * Pure arithmetic, tested on its own.
 */
class Ducker {
public:
	void set(const DuckSettings &settings) { settings_ = settings; }
	const DuckSettings &settings() const { return settings_; }

	/* `trigger_peak` is linear, 1 = full scale. Returns linear gain. */
	float process(float trigger_peak, size_t frames, uint32_t sample_rate);

	float gain() const;
	float gain_db() const { return gain_db_; }
	bool ducking() const { return gain_db_ < -0.01f; }

private:
	DuckSettings settings_;
	float gain_db_ = 0.0f;
	float hold_left_ms_ = 0.0f;
};

} // namespace vr
