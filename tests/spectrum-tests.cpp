/*
 * Tests for the visualiser's analysis: a pure tone lands in the band that
 * holds its frequency, loudness maps onto the level scale, and smoothing rises
 * fast and falls slowly.
 */

#include "sources/music-widget/spectrum.hpp"
#include "test-check.hpp"

#include <cmath>
#include <vector>

using namespace vr::widget;
using test::check;
using test::check_near;
using test::check_true;

namespace {

constexpr uint32_t RATE = 48000;
constexpr float PI_F = 3.14159265358979323846f;

std::vector<float> tone(float hz, float amplitude, size_t count = SPECTRUM_WINDOW)
{
	std::vector<float> out(count);
	for (size_t i = 0; i < count; i++)
		out[i] = amplitude * std::sin(2.0f * PI_F * hz * (float)i / (float)RATE);
	return out;
}

size_t loudest(const std::vector<float> &levels)
{
	size_t best = 0;
	for (size_t i = 1; i < levels.size(); i++)
		if (levels[i] > levels[best])
			best = i;
	return best;
}

/* The band a frequency falls in, by the same spacing the analyser uses. */
size_t band_of(float hz, int bands, float low, float high)
{
	return (size_t)std::floor(std::log(hz / low) / std::log(high / low) * (float)bands);
}

} // namespace

int main()
{
	Spectrum spectrum;
	std::vector<float> levels;

	/* Silence is level 0 everywhere. */
	{
		std::vector<float> silence(SPECTRUM_WINDOW, 0.0f);
		spectrum.analyse(silence.data(), silence.size(), RATE, 16, 40.0f, 16000.0f, 1.0f, levels);
		check("silence: band count", (long long)levels.size(), 16);
		float peak = 0.0f;
		for (float level : levels)
			peak = std::max(peak, level);
		check_near("silence: nothing shows", peak, 0.0, 1e-6);
	}

	/* A tone lights up its own band. */
	for (float hz : {110.0f, 1000.0f, 6000.0f}) {
		const std::vector<float> signal = tone(hz, 0.5f);
		spectrum.analyse(signal.data(), signal.size(), RATE, 24, 40.0f, 16000.0f, 1.0f, levels);
		const long long want = (long long)band_of(hz, 24, 40.0f, 16000.0f);
		const long long got = (long long)loudest(levels);
		check_true(("tone " + std::to_string((int)hz) + " Hz: right band").c_str(),
			   std::llabs(got - want) <= 1);
	}

	/* A full scale sine reads near the top; -30 dB reads about half way. */
	{
		const std::vector<float> loud = tone(1000.0f, 1.0f);
		spectrum.analyse(loud.data(), loud.size(), RATE, 24, 40.0f, 16000.0f, 1.0f, levels);
		check_near("full scale: near 1", levels[loudest(levels)], 1.0, 0.08);

		const std::vector<float> quiet = tone(1000.0f, std::pow(10.0f, -30.0f / 20.0f));
		spectrum.analyse(quiet.data(), quiet.size(), RATE, 24, 40.0f, 16000.0f, 1.0f, levels);
		check_near("-30 dB: about half", levels[loudest(levels)], 0.5, 0.08);

		/* Gain lifts the same quiet signal. */
		spectrum.analyse(quiet.data(), quiet.size(), RATE, 24, 40.0f, 16000.0f, 4.0f, levels);
		check_true("gain lifts the level", levels[loudest(levels)] > 0.55f);
	}

	/* A short buffer still works, padded with silence. */
	{
		const std::vector<float> brief = tone(440.0f, 0.5f, 300);
		spectrum.analyse(brief.data(), brief.size(), RATE, 12, 40.0f, 16000.0f, 1.0f, levels);
		check("short buffer: band count", (long long)levels.size(), 12);
	}

	/* The waveform keeps the swing of each slice. */
	{
		const std::vector<float> signal = tone(50.0f, 0.8f);
		std::vector<float> points;
		wave_points(signal.data(), signal.size(), 32, 1.0f, points);
		check("wave: point count", (long long)points.size(), 32);
		float peak = 0.0f;
		for (float p : points)
			peak = std::max(peak, std::fabs(p));
		check_near("wave: peaks kept", peak, 0.8, 0.05);

		wave_points(signal.data(), signal.size(), 32, 10.0f, points);
		float clipped = 0.0f;
		for (float p : points)
			clipped = std::max(clipped, std::fabs(p));
		check_near("wave: clamped to 1", clipped, 1.0, 1e-6);
	}

	/* Smoothing: up fast, down slowly, and slower with more smoothing. */
	{
		std::vector<float> shown;
		const std::vector<float> high(4, 1.0f);
		const std::vector<float> low(4, 0.0f);

		smooth_levels(shown, high, 1.0f / 30.0f, 0.6f);
		check_true("rise: most of the way in a frame", shown[0] > 0.6f);

		for (int i = 0; i < 60; i++)
			smooth_levels(shown, high, 1.0f / 60.0f, 0.6f);

		std::vector<float> light = shown;
		std::vector<float> heavy = shown;
		smooth_levels(light, low, 1.0f / 30.0f, 0.1f);
		smooth_levels(heavy, low, 1.0f / 30.0f, 0.9f);
		check_true("fall: lighter smoothing falls further", light[0] < heavy[0]);
		check_true("fall: heavy smoothing still falls", heavy[0] < 1.0f);
	}

	return test::finish();
}
