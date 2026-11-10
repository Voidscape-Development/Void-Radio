/*
 * Tests for the ducker: it goes down when the trigger is loud, by the depth
 * asked for and in about the attack time, holds through short pauses, and
 * comes back in about the release time.
 */

#include "music/ducker.hpp"
#include "test-check.hpp"

#include <cmath>

using test::check_near;
using test::check_true;

namespace {

constexpr uint32_t RATE = 48000;
constexpr size_t BLOCK = 480; /* 10 ms */

float run(vr::Ducker &ducker, float peak, int ms)
{
	float gain = ducker.gain();
	for (int done = 0; done < ms; done += 10)
		gain = ducker.process(peak, BLOCK, RATE);
	return gain;
}

float db(float gain)
{
	return 20.0f * std::log10(gain);
}

} // namespace

int main()
{
	vr::DuckSettings settings;
	settings.enabled = true;
	settings.threshold_db = -30.0f;
	settings.depth_db = 12.0f;
	settings.attack_ms = 100;
	settings.hold_ms = 300;
	settings.release_ms = 600;

	const float loud = std::pow(10.0f, -10.0f / 20.0f);  /* -10 dB: talking */
	const float quiet = std::pow(10.0f, -50.0f / 20.0f); /* -50 dB: room noise */

	/* Quiet room: nothing happens. */
	{
		vr::Ducker ducker;
		ducker.set(settings);
		check_near("quiet: full volume", run(ducker, quiet, 1000), 1.0, 1e-6);
	}

	/* Talking: down by the depth, in about the attack time. */
	{
		vr::Ducker ducker;
		ducker.set(settings);
		const float half = run(ducker, loud, 50);
		check_near("attack: half way after half the time", db(half), -6.0, 0.6);
		const float down = run(ducker, loud, 100);
		check_near("attack: all the way down", db(down), -12.0, 0.01);
		check_true("ducking", ducker.ducking());

		/* A short pause between words holds it down. */
		check_near("hold: stays down through a pause", db(run(ducker, quiet, 250)), -12.0, 0.01);

		/* Then it comes back over the release time. */
		run(ducker, quiet, 50); /* the rest of the hold */
		const float rising = run(ducker, quiet, 300);
		check_near("release: half way up", db(rising), -6.0, 0.8);
		check_near("release: all the way back", run(ducker, quiet, 400), 1.0, 1e-4);
		check_true("not ducking", !ducker.ducking());
	}

	/* Switching it off brings the music back, gently. */
	{
		vr::Ducker ducker;
		ducker.set(settings);
		run(ducker, loud, 200);
		settings.enabled = false;
		ducker.set(settings);
		const float first = ducker.process(loud, BLOCK, RATE);
		check_true("off: does not jump straight back", db(first) < -11.0f);
		check_near("off: comes back", run(ducker, loud, 700), 1.0, 1e-4);
		settings.enabled = true;
	}

	/* Silence from the trigger (no audio at all) counts as quiet. */
	{
		vr::Ducker ducker;
		ducker.set(settings);
		check_near("no trigger audio: full volume", run(ducker, 0.0f, 500), 1.0, 1e-6);
	}

	return test::finish();
}
