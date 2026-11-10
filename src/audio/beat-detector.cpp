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

#include "audio/beat-detector.hpp"

#include <algorithm>
#include <cmath>

namespace vr {

namespace {

constexpr double PI_D = 3.14159265358979323846;
constexpr double BASS_HZ = 200.0;
constexpr double TREBLE_HZ = 4000.0;
constexpr double HISTORY_SECONDS = 0.1;
constexpr double MIN_BEAT_GAP = 0.15; /* 400 bpm; anything closer is one beat */
constexpr double FLOOR_DB = -60.0;
constexpr size_t TEMPO_BEATS = 12;

/* How quickly the last hit's level stops counting against the next. */
constexpr double PEAK_FALL_SECONDS = 0.25;

/* RBJ cookbook second order sections. */
void design(double rate, double hz, bool high, double &b0, double &b1, double &b2, double &a1, double &a2)
{
	const double w = 2.0 * PI_D * hz / rate;
	const double alpha = std::sin(w) / (2.0 * 0.7071);
	const double cosw = std::cos(w);
	const double a0 = 1.0 + alpha;

	if (high) {
		b0 = (1.0 + cosw) / 2.0 / a0;
		b1 = -(1.0 + cosw) / a0;
		b2 = b0;
	} else {
		b0 = (1.0 - cosw) / 2.0 / a0;
		b1 = (1.0 - cosw) / a0;
		b2 = b0;
	}
	a1 = -2.0 * cosw / a0;
	a2 = (1.0 - alpha) / a0;
}

float to_unit(double energy)
{
	if (energy <= 0.0)
		return 0.0f;
	const double db = 10.0 * std::log10(energy);
	return (float)std::clamp((db - FLOOR_DB) / -FLOOR_DB, 0.0, 1.0);
}

} // namespace

BeatDetector::BeatDetector(uint32_t sample_rate)
	: rate_(std::max<uint32_t>(sample_rate, 8000)),
	  step_frames_(std::max<size_t>(1, rate_ / 100)),
	  history_((size_t)(HISTORY_SECONDS * 100.0), 0.0)
{
	design(rate_, BASS_HZ, false, low_.b0, low_.b1, low_.b2, low_.a1, low_.a2);
	design(rate_, TREBLE_HZ, true, high_.b0, high_.b1, high_.b2, high_.a1, high_.a2);
}

float BeatDetector::envelope(float current, float target, float seconds) const
{
	/* Up at once, down over `seconds`, so hits land on time. */
	if (target >= current)
		return target;
	const float step = 0.01f;
	const float keep = std::exp(-step / std::max(0.001f, seconds));
	return target + (current - target) * keep;
}

void BeatDetector::process(const float *mono, size_t frames)
{
	for (size_t i = 0; i < frames; i++) {
		const double x = mono[i];
		const double low = low_.run(x);
		const double high = high_.run(x);

		sum_all_ += x * x;
		sum_low_ += low * low;
		sum_high_ += high * high;

		if (++filled_ == step_frames_)
			finish_step();
	}
}

void BeatDetector::finish_step()
{
	const double n = (double)step_frames_;
	const double all = sum_all_ / n;
	const double low = sum_low_ / n;
	const double high = sum_high_ / n;
	sum_all_ = sum_low_ = sum_high_ = 0.0;
	filled_ = 0;
	steps_++;

	/* Mean squares of a full scale sine are 0.5; doubling reads 0 dB. */
	level_ = envelope(level_, to_unit(all * 2.0), 0.15f);
	bass_ = envelope(bass_, to_unit(low * 2.0), 0.15f);
	treble_ = envelope(treble_, to_unit(high * 2.0), 0.10f);

	/* A beat: the bass jumps sharply against what it was a moment ago, and
	 * is loud enough to be more than noise. Comparing with the last few
	 * steps rather than a long average is what finds a kick over a busy,
	 * sustained bass line, while a held note, however loud, never rises
	 * and so never fires. The more sensitive, the smaller the jump. */
	double before = low;
	for (size_t back = 1; back <= 3 && back <= history_count_; back++) {
		const size_t at = (history_at_ + history_.size() - back) % history_.size();
		before = std::min(before, history_[at]);
	}

	const double rise_db = before > 0.0 ? 10.0 * std::log10(low / before) : (low > 0.0 ? 60.0 : 0.0);
	const double needed_db = 4.0 / std::max(0.1f, sensitivity_);
	const bool loud = to_unit(low * 2.0) > 0.25f;
	const bool settled = history_count_ >= 3;
	const double since = (double)(steps_ - last_beat_step_) / 100.0;

	/* And it has to top the last hit as it dies away, so the wobble of a
	 * kick ringing against the bass line is not taken for another. */
	const double gate = peak_;
	peak_ = std::max(peak_ * std::exp(-0.01 / PEAK_FALL_SECONDS), low);

	if (settled && loud && rise_db >= needed_db && low > gate && (beats_ == 0 || since >= MIN_BEAT_GAP)) {
		if (beats_ > 0 && since < 2.0) {
			intervals_.push_back((float)since);
			if (intervals_.size() > TEMPO_BEATS)
				intervals_.erase(intervals_.begin());
		}
		beats_++;
		last_beat_step_ = steps_;
		pulse_ = 1.0f;
	} else {
		pulse_ *= std::exp(-0.01f / std::max(0.01f, pulse_decay_));
	}

	history_[history_at_] = low;
	history_at_ = (history_at_ + 1) % history_.size();
	history_count_ = std::min(history_count_ + 1, history_.size());
}

void BeatDetector::idle(float seconds)
{
	const float fall = std::exp(-seconds / 0.2f);
	level_ *= fall;
	bass_ *= fall;
	treble_ *= fall;
	pulse_ *= std::exp(-seconds / std::max(0.01f, pulse_decay_));
	peak_ *= std::exp(-seconds / PEAK_FALL_SECONDS);
}

float BeatDetector::bpm() const
{
	if (intervals_.size() < 4)
		return 0.0f;

	std::vector<float> sorted = intervals_;
	std::sort(sorted.begin(), sorted.end());
	const float median = sorted[sorted.size() / 2];
	return median > 0.0f ? 60.0f / median : 0.0f;
}

} // namespace vr
