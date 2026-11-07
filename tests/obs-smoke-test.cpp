/*
 * Loads the built plugin into a real, headless libobs and plays a short
 * playlist through the Music source, checking what OBS actually receives.
 *
 * This is the one test that exercises the plugin as OBS runs it: module
 * loading, the engine thread, output timestamps, OBS's media controls. It needs
 * libobs and, on Linux, an X display (run it under xvfb-run). It is only built
 * when the tests can find libobs, and only runs when VOID_RADIO_PLUGIN names the
 * built module:
 *
 *   cmake -S tests -B build_tests -DVOID_RADIO_PLUGIN=build_x86_64/void-radio.so
 *   xvfb-run -a ctest --test-dir build_tests -R obs-smoke --output-on-failure
 */

#include "media-fixtures.hpp"
#include "test-check.hpp"

#include <obs.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

using test::check;
using test::check_true;

namespace {

constexpr uint32_t RATE = 48000;

std::atomic<uint64_t> frames_received{0};
std::atomic<int> discontinuities{0};
uint64_t last_timestamp = 0;
uint64_t last_frames = 0;

void capture(void *, obs_source_t *, const struct audio_data *audio, bool)
{
	if (last_timestamp) {
		const uint64_t expected = last_timestamp + last_frames * 1000000000ULL / RATE;
		const int64_t drift = (int64_t)audio->timestamp - (int64_t)expected;
		if (drift > 2000000 || drift < -2000000)
			discontinuities++;
	}

	last_timestamp = audio->timestamp;
	last_frames = audio->frames;
	frames_received += audio->frames;
}

void wait_ms(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool near(int64_t got, int64_t want, int64_t tolerance)
{
	return got >= want - tolerance && got <= want + tolerance;
}

std::string write_track(const char *name, int duration_ms)
{
	fixture::Spec spec;
	spec.format = "mp3";
	spec.codec = "libmp3lame";

	/* An MP3 with cover art is exactly the file the seek bug hit. */
	if (!fixture::supported(spec)) {
		spec.format = "flac";
		spec.codec = "flac";
	}

	spec.duration_ms = duration_ms;
	spec.pictures = {"Cover (front)"};

	const std::string path = fixture::temp_path(std::string(name) + "." + spec.format);
	fixture::write(path, spec);
	return path;
}

} // namespace

int main()
{
	const char *plugin = getenv("VOID_RADIO_PLUGIN");
	const char *data = getenv("VOID_RADIO_DATA");
	if (!plugin || !*plugin) {
		printf("VOID_RADIO_PLUGIN is not set; nothing to test\n");
		return 0;
	}

	const std::string first = write_track("smoke-1", 3000);
	const std::string second = write_track("smoke-2", 2000);

	check_true("obs starts", obs_startup("en-US", nullptr, nullptr));

	struct obs_audio_info audio_info = {RATE, SPEAKERS_STEREO};
	check_true("audio resets", obs_reset_audio(&audio_info));

	obs_module_t *module = nullptr;
	check_true("plugin opens", obs_open_module(&module, plugin, data) == MODULE_SUCCESS);
	check_true("plugin loads", module && obs_init_module(module));

	obs_data_t *settings = obs_data_create();
	obs_data_array_t *playlist = obs_data_array_create();
	for (const std::string &path : {first, second}) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "value", path.c_str());
		obs_data_array_push_back(playlist, item);
		obs_data_release(item);
	}
	obs_data_set_array(settings, "playlist", playlist);
	obs_data_set_bool(settings, "play_on_start", true);
	obs_data_set_int(settings, "repeat", 0);

	obs_source_t *music = obs_source_create("void_radio_music_source", "music", settings, nullptr);
	check_true("music source created", music != nullptr);
	if (!music)
		return test::finish();

	obs_source_add_audio_capture_callback(music, capture, nullptr);

	wait_ms(1000);
	check("plays on start", obs_source_media_get_state(music), OBS_MEDIA_STATE_PLAYING);
	check_true("first track's duration", near(obs_source_media_get_duration(music), 3000, 60));
	check_true("audio reaches OBS", frames_received > RATE / 2);

	/* The original bug: a seek moved on to the next track instead. */
	obs_source_media_set_time(music, 2000);
	wait_ms(400);
	check_true("seek stays on the track", near(obs_source_media_get_duration(music), 3000, 60));
	check_true("seek lands", near(obs_source_media_get_time(music), 2400, 150));

	wait_ms(900);
	check_true("then moves on at the end", near(obs_source_media_get_duration(music), 2000, 60));

	obs_source_media_play_pause(music, true);
	wait_ms(200);
	const uint64_t paused_frames = frames_received;
	wait_ms(300);
	check("paused", obs_source_media_get_state(music), OBS_MEDIA_STATE_PAUSED);
	check("nothing sent while paused", (long long)(frames_received - paused_frames), 0);

	obs_source_media_play_pause(music, false);
	last_timestamp = 0; /* a resume starts a new run of audio */
	wait_ms(300);
	check("resumes", obs_source_media_get_state(music), OBS_MEDIA_STATE_PLAYING);

	obs_source_media_set_time(music, 1900);
	wait_ms(700);
	check("stops after the last track", obs_source_media_get_state(music), OBS_MEDIA_STATE_STOPPED);
	check("timestamps continuous", discontinuities.load(), 0);

	obs_source_remove_audio_capture_callback(music, capture, nullptr);
	obs_source_release(music);
	obs_data_array_release(playlist);
	obs_data_release(settings);
	obs_shutdown();

	remove(first.c_str());
	remove(second.c_str());

	return test::finish();
}
