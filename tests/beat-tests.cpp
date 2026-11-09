/*
 * Tests for the beat detector: a kick drum at 120 bpm under a steady hi-hat
 * gives two beats a second and a tempo of 120, a steady tone gives none, and
 * the pulse and levels fall back to rest in silence.
 */

#include "audio/beat-detector.hpp"
#include "test-check.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using test::check;
using test::check_near;
using test::check_true;

namespace {

constexpr uint32_t RATE = 48000;
constexpr double PI_D = 3.14159265358979323846;

/* A kick (a falling 60 Hz thump) every `period` seconds, over hi-hat noise
 * and a soft pad, the way a dance track is built. */
std::vector<float> groove(double seconds, double period, double kick_level = 0.8)
{
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
	std::vector<float> out((size_t)(seconds * RATE));

	for (size_t i = 0; i < out.size(); i++) {
		const double t = (double)i / RATE;
		const double in_beat = std::fmod(t, period);
		const double kick = kick_level * std::sin(2.0 * PI_D * 60.0 * in_beat) * std::exp(-in_beat * 18.0);
		const double hat = 0.05 * noise(rng) * std::exp(-std::fmod(t, period / 2) * 40.0);
		const double pad = 0.08 * std::sin(2.0 * PI_D * 440.0 * t);
		out[i] = (float)(kick + hat + pad);
	}
	return out;
}

/* A kick that sweeps down from 165 Hz, over a loud sustained bass line and
 * chords. The bass line keeps the low end busy all the time, which is what
 * makes the kicks hard to tell apart from it. */
std::vector<float> busy_groove(double seconds)
{
	std::mt19937 rng(3);
	std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
	std::vector<float> out((size_t)(seconds * RATE));
	const double roots[4] = {110.0, 87.3, 130.8, 98.0};

	for (size_t i = 0; i < out.size(); i++) {
		const double t = (double)i / RATE;
		const double beat = std::fmod(t, 0.5);
		const double kick = std::sin(2.0 * PI_D * 55.0 * beat * (1.0 + 2.0 * std::exp(-beat * 30.0))) *
				    std::exp(-beat * 9.0) * 0.6;
		const double root = roots[(int)(t / 2.0) % 4];
		const double bass = 0.25 * std::sin(2.0 * PI_D * root * t);
		double chord = 0.0;
		for (double m : {1.0, 1.26, 1.5})
			chord += 0.07 * std::sin(2.0 * PI_D * root * m * 2.0 * t);
		const double hat = 0.12 * noise(rng) * std::exp(-std::fmod(t, 0.25) * 60.0);
		out[i] = (float)std::clamp(kick + bass + chord + hat, -1.0, 1.0);
	}
	return out;
}

void feed(vr::BeatDetector &detector, const std::vector<float> &samples)
{
	/* In OBS-sized pieces. */
	for (size_t at = 0; at < samples.size(); at += 1024)
		detector.process(samples.data() + at, std::min<size_t>(1024, samples.size() - at));
}

} // namespace

int main()
{
	/* 120 bpm for ten seconds: about twenty beats, and the tempo. */
	{
		vr::BeatDetector detector(RATE);
		feed(detector, groove(10.0, 0.5));
		const long long beats = (long long)detector.beats();
		check_true("120 bpm: about twenty beats", beats >= 18 && beats <= 21);
		check_near("120 bpm: tempo", detector.bpm(), 120.0, 3.0);
	}

	/* A quieter track still has beats; the average follows the music. */
	{
		vr::BeatDetector detector(RATE);
		feed(detector, groove(10.0, 0.6, 0.15));
		check_true("quiet 100 bpm: beats found", detector.beats() >= 14);
		check_near("quiet 100 bpm: tempo", detector.bpm(), 100.0, 3.0);
	}

	/* Kicks over a busy, sustained bass line are still found. */
	{
		vr::BeatDetector detector(RATE);
		feed(detector, busy_groove(10.0));
		const long long beats = (long long)detector.beats();
		check_true("busy groove: about twenty beats", beats >= 18 && beats <= 21);
		check_near("busy groove: tempo", detector.bpm(), 120.0, 3.0);
	}

	/* A steady tone, bass included, is not a beat. */
	{
		vr::BeatDetector detector(RATE);
		std::vector<float> tone((size_t)(5.0 * RATE));
		for (size_t i = 0; i < tone.size(); i++)
			tone[i] = (float)(0.5 * std::sin(2.0 * PI_D * 80.0 * (double)i / RATE));
		feed(detector, tone);
		check_true("steady bass tone: no beats", detector.beats() <= 1);
		check_true("steady bass tone: bass is high", detector.bass() > 0.8f);
		check_true("steady bass tone: treble is low", detector.treble() < 0.3f);
	}

	/* The pulse jumps on a beat and dies away; silence brings it all down. */
	{
		vr::BeatDetector detector(RATE);
		detector.set_pulse_decay(0.2f);
		feed(detector, groove(3.0, 0.5));
		const std::vector<float> one_kick = groove(0.05, 0.5);
		feed(detector, one_kick);
		check_true("pulse: high just after a beat", detector.pulse() > 0.6f);

		std::vector<float> silence((size_t)(1.0 * RATE), 0.0f);
		feed(detector, silence);
		check_true("pulse: gone after a second of silence", detector.pulse() < 0.05f);
		check_true("level: gone in silence", detector.level() < 0.05f);

		vr::BeatDetector paused(RATE);
		feed(paused, groove(2.0, 0.5));
		paused.idle(1.0f);
		check_true("idle: everything falls back", paused.level() < 0.05f && paused.pulse() < 0.05f);
	}

	return test::finish();
}
