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
 * Follows music as it plays and says how hard it is hitting right now: the
 * overall level, the bass, the treble, and a pulse that jumps to 1 on each
 * beat and dies away. A beat is a sudden rise in bass energy against the
 * last few hundredths of a second, so a held bass note does not fire however
 * loud it is, a kick over a busy bass line still does, and a quiet song still
 * has beats. Also estimates the tempo from the gaps between beats.
 *
 * Works in 10 ms steps of mono audio. Pure arithmetic, tested on its own.
 */
class BeatDetector {
public:
	explicit BeatDetector(uint32_t sample_rate = 48000);

	/* 1 is normal; higher catches softer beats. */
	void set_sensitivity(float sensitivity) { sensitivity_ = sensitivity; }

	/* How long a beat's pulse takes to die away, in seconds. */
	void set_pulse_decay(float seconds) { pulse_decay_ = seconds; }

	void process(const float *mono, size_t frames);

	/* Lets everything fall back to rest when no audio is arriving, as
	 * while paused. */
	void idle(float seconds);

	/* 0..1. */
	float level() const { return level_; }
	float bass() const { return bass_; }
	float treble() const { return treble_; }
	float pulse() const { return pulse_; }

	uint64_t beats() const { return beats_; }

	/* Beats per minute from recent beats, or 0 until there are enough. */
	float bpm() const;

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
	float envelope(float current, float target, float seconds) const;

	uint32_t rate_;
	size_t step_frames_;

	Biquad low_;
	Biquad high_;

	size_t filled_ = 0;
	double sum_all_ = 0.0;
	double sum_low_ = 0.0;
	double sum_high_ = 0.0;

	/* The last tenth of a second of bass energy, to spot a sudden rise. */
	std::vector<double> history_;
	size_t history_at_ = 0;
	size_t history_count_ = 0;
	double peak_ = 0.0; /* the last hit's bass energy, dying away */

	float sensitivity_ = 1.0f;
	float pulse_decay_ = 0.25f;

	float level_ = 0.0f;
	float bass_ = 0.0f;
	float treble_ = 0.0f;
	float pulse_ = 0.0f;

	uint64_t steps_ = 0;
	uint64_t last_beat_step_ = 0;
	uint64_t beats_ = 0;
	std::vector<float> intervals_; /* seconds between recent beats */
};

} // namespace vr
