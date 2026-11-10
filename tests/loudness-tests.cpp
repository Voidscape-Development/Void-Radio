/*
 * Tests for the loudness meter against the reference points EBU Tech 3341
 * gives: a stereo 1 kHz sine at -23 dBFS reads -23 LUFS, gating ignores
 * silence, and quieter audio reads lower by the same number of decibels.
 */

#include "audio/loudness.hpp"
#include "test-check.hpp"

#include <cmath>
#include <vector>

using test::check_near;
using test::check_true;

namespace {

constexpr double PI_D = 3.14159265358979323846;

void feed_sine(vr::LoudnessMeter &meter, uint32_t rate, double hz, double dbfs, double seconds)
{
	const double amplitude = std::pow(10.0, dbfs / 20.0);
	const size_t frames = (size_t)(rate * seconds);
	std::vector<float> left(frames), right(frames);

	for (size_t i = 0; i < frames; i++)
		left[i] = right[i] = (float)(amplitude * std::sin(2.0 * PI_D * hz * (double)i / rate));

	/* In uneven chunks, as audio really arrives. */
	size_t done = 0;
	size_t chunk = 333;
	while (done < frames) {
		const size_t take = std::min(chunk, frames - done);
		const float *planes[2] = {left.data() + done, right.data() + done};
		meter.add(planes, take);
		done += take;
		chunk = chunk == 333 ? 1024 : 333;
	}
}

void feed_silence(vr::LoudnessMeter &meter, uint32_t rate, double seconds)
{
	const size_t frames = (size_t)(rate * seconds);
	std::vector<float> zero(frames, 0.0f);
	const float *planes[2] = {zero.data(), zero.data()};
	meter.add(planes, frames);
}

} // namespace

int main()
{
	for (uint32_t rate : {44100u, 48000u, 96000u}) {
		vr::LoudnessMeter meter(rate, 2);
		feed_sine(meter, rate, 1000.0, -23.0, 20.0);
		const std::string label = "1 kHz -23 dBFS at " + std::to_string(rate) + " Hz";
		check_near(label.c_str(), meter.integrated(), -23.0, 0.1);
		check_near(("peak at " + std::to_string(rate)).c_str(), 20.0 * std::log10(meter.peak()), -23.0, 0.05);
	}

	/* Silence either side does not drag the reading down. */
	{
		vr::LoudnessMeter meter(48000, 2);
		feed_silence(meter, 48000, 10.0);
		feed_sine(meter, 48000, 1000.0, -23.0, 10.0);
		feed_silence(meter, 48000, 10.0);
		check_near("gated: silence ignored", meter.integrated(), -23.0, 0.2);
	}

	/* 10 dB quieter reads 10 LU lower. */
	{
		vr::LoudnessMeter meter(48000, 2);
		feed_sine(meter, 48000, 1000.0, -33.0, 10.0);
		check_near("quieter: -33", meter.integrated(), -33.0, 0.1);
	}

	/* Low frequencies count for less, as the ear hears them. */
	{
		vr::LoudnessMeter low(48000, 2);
		feed_sine(low, 48000, 40.0, -23.0, 10.0);
		check_true("40 Hz reads quieter than 1 kHz", low.integrated() < -23.5);
	}

	/* Nothing heard: no reading. */
	{
		vr::LoudnessMeter meter(48000, 2);
		feed_silence(meter, 48000, 5.0);
		check_true("silence: no reading", std::isnan(meter.integrated()));
	}

	return test::finish();
}
