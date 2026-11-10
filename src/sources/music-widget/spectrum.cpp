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

#include "sources/music-widget/spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace vr::widget {

namespace {

constexpr float PI_F = 3.14159265358979323846f;
constexpr float FLOOR_DB = -60.0f;

/* In place, iterative radix-2. `data.size()` is a power of two. */
void fft(std::vector<std::complex<float>> &data)
{
	const size_t n = data.size();

	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(data[i], data[j]);
	}

	for (size_t length = 2; length <= n; length <<= 1) {
		const float angle = -2.0f * PI_F / (float)length;
		const std::complex<float> step(std::cos(angle), std::sin(angle));

		for (size_t start = 0; start < n; start += length) {
			std::complex<float> w(1.0f, 0.0f);
			for (size_t k = 0; k < length / 2; k++) {
				const std::complex<float> even = data[start + k];
				const std::complex<float> odd = data[start + k + length / 2] * w;
				data[start + k] = even + odd;
				data[start + k + length / 2] = even - odd;
				w *= step;
			}
		}
	}
}

float to_level(float amplitude)
{
	if (amplitude <= 0.0f)
		return 0.0f;

	const float db = 20.0f * std::log10(amplitude);
	return std::clamp((db - FLOOR_DB) / -FLOOR_DB, 0.0f, 1.0f);
}

} // namespace

void Spectrum::analyse(const float *samples, size_t count, uint32_t sample_rate, int bands, float min_freq,
		       float max_freq, float gain, std::vector<float> &out)
{
	bands = std::max(1, bands);
	out.assign((size_t)bands, 0.0f);

	if (!samples || count == 0 || sample_rate == 0)
		return;

	const size_t n = SPECTRUM_WINDOW;

	if (window_.size() != n) {
		window_.resize(n);
		for (size_t i = 0; i < n; i++)
			window_[i] = 0.5f - 0.5f * std::cos(2.0f * PI_F * (float)i / (float)(n - 1));
	}

	buffer_.assign(n, std::complex<float>(0.0f, 0.0f));

	/* The newest samples land at the end, so a short buffer is padded with
	 * silence at the start rather than the end. */
	const size_t used = std::min(count, n);
	const float *from = samples + (count - used);
	for (size_t i = 0; i < used; i++) {
		const size_t at = n - used + i;
		buffer_[at] = std::complex<float>(from[i] * gain * window_[at], 0.0f);
	}

	fft(buffer_);

	/* A Hann window halves a sine's peak, so 4/N brings a full scale sine
	 * back to an amplitude of 1. */
	const float scale = 4.0f / (float)n;
	const size_t bins = n / 2;
	const float bin_hz = (float)sample_rate / (float)n;

	const float nyquist = (float)sample_rate * 0.5f;
	const float low = std::clamp(min_freq, bin_hz, nyquist);
	const float high = std::clamp(max_freq, low * 1.01f, nyquist);
	const float ratio = high / low;

	for (int band = 0; band < bands; band++) {
		const float f0 = low * std::pow(ratio, (float)band / (float)bands);
		const float f1 = low * std::pow(ratio, (float)(band + 1) / (float)bands);

		size_t b0 = (size_t)std::floor(f0 / bin_hz);
		size_t b1 = (size_t)std::ceil(f1 / bin_hz);
		b0 = std::clamp<size_t>(b0, 1, bins - 1);
		b1 = std::clamp<size_t>(b1, b0 + 1, bins);

		/* Narrow low bands can fall between bins; the widest swing in or
		 * next to the band keeps them from flickering to nothing. */
		float peak = 0.0f;
		for (size_t bin = b0; bin < b1; bin++)
			peak = std::max(peak, std::abs(buffer_[bin]) * scale);

		out[(size_t)band] = to_level(peak);
	}
}

void wave_points(const float *samples, size_t count, int points, float gain, std::vector<float> &out)
{
	points = std::max(2, points);
	out.assign((size_t)points, 0.0f);

	if (!samples || count == 0)
		return;

	const size_t used = std::min(count, SPECTRUM_WINDOW);
	const float *from = samples + (count - used);

	for (int p = 0; p < points; p++) {
		const size_t start = used * (size_t)p / (size_t)points;
		const size_t end = std::max(start + 1, used * (size_t)(p + 1) / (size_t)points);

		float swing = 0.0f;
		for (size_t i = start; i < end && i < used; i++) {
			if (std::fabs(from[i]) > std::fabs(swing))
				swing = from[i];
		}

		out[(size_t)p] = std::clamp(swing * gain, -1.0f, 1.0f);
	}
}

void smooth_levels(std::vector<float> &shown, const std::vector<float> &target, float seconds, float smoothing)
{
	if (shown.size() != target.size())
		shown.assign(target.size(), 0.0f);

	smoothing = std::clamp(smoothing, 0.0f, 0.99f);

	/* Rates per second, turned into a per-frame step that does not depend
	 * on the frame rate. */
	const float rise = 1.0f - std::exp(-seconds * 40.0f);
	const float fall = 1.0f - std::exp(-seconds * (1.0f - smoothing) * 18.0f);

	for (size_t i = 0; i < target.size(); i++) {
		const float rate = target[i] > shown[i] ? rise : fall;
		shown[i] += (target[i] - shown[i]) * rate;
	}
}

} // namespace vr::widget
