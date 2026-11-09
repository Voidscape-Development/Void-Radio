/*
 * Transport tests for vr::Player, driven by fake decks.
 *
 * The output runs at 1000 Hz so that one sample is one millisecond, and every
 * fake deck plays a constant level equal to its track number, so the mix shows
 * exactly which tracks are audible and how loud.
 */

#include "music/player.hpp"
#include "test-check.hpp"

#include <cmath>
#include <map>
#include <memory>
#include <vector>

using test::check;
using test::check_near;
using test::check_true;

namespace {

constexpr uint32_t RATE = 1000;
constexpr size_t BLOCK = 10;

struct DeckScript {
	int64_t duration_ms = 4000;
	bool fail = false;      /* never opens */
	int open_reads = 0;     /* reads that return nothing before audio starts */
	int seek_lag_reads = 0; /* reads that return nothing after a seek */
	bool report_duration = true;
	bool seekable = true;
	int64_t lead_silence_ms = 0; /* silent before the track's level starts */
	int64_t tail_silence_ms = 0; /* silent for this long at the end */
};

class FakeDeck final : public vr::Deck {
public:
	FakeDeck(float level, const DeckScript &script) : level_(level), script_(script), opening_(script.open_reads) {}

	State state() const override
	{
		if (script_.fail)
			return State::Failed;
		if (opening_ > 0)
			return State::Opening;
		if (lag_ == 0 && position_ >= script_.duration_ms)
			return State::Ended;
		return State::Ready;
	}

	size_t read(float *const *out, size_t frames) override
	{
		if (script_.fail)
			return 0;
		if (opening_ > 0) {
			opening_--;
			return 0;
		}
		if (lag_ > 0) {
			lag_--;
			return 0;
		}

		const int64_t left = script_.duration_ms - position_;
		const size_t got = left <= 0 ? 0 : std::min<size_t>(frames, (size_t)left);

		for (size_t ch = 0; ch < 2; ch++)
			for (size_t i = 0; i < got; i++) {
				const int64_t at = position_ + (int64_t)i;
				const bool silent = at < script_.lead_silence_ms ||
						    at >= script_.duration_ms - script_.tail_silence_ms;
				out[ch][i] = silent ? 0.0f : level_;
			}

		position_ += (int64_t)got;
		if (got)
			produced_ = true;
		return got;
	}

	void seek(int64_t ms) override
	{
		seeks++;
		position_ = std::min<int64_t>(std::max<int64_t>(0, ms), script_.duration_ms);
		lag_ = script_.seek_lag_reads;
	}

	int64_t position_ms() const override { return position_; }
	int64_t duration_ms() const override
	{
		return script_.report_duration && !script_.fail ? script_.duration_ms : 0;
	}
	bool seekable() const override { return script_.seekable; }
	bool produced() const override { return produced_; }

	int seeks = 0;

private:
	float level_;
	DeckScript script_;
	int opening_;
	int lag_ = 0;
	int64_t position_ = 0;
	bool produced_ = false;
};

/* A player over `count` tracks named /t/1 .. /t/N, with a script per track. */
struct Rig {
	explicit Rig(int count, DeckScript script = {}, vr::PlayerSettings settings = {})
		: player({RATE, 2, 0}, [this](const vr::PlaylistEntry &entry) { return make(entry); })
	{
		std::vector<vr::PlaylistEntry> entries;
		for (int i = 1; i <= count; i++) {
			vr::PlaylistEntry entry;
			entry.path = "/t/" + std::to_string(i);
			entries.push_back(entry);
			scripts[entry.path] = script;
		}

		settings.shuffle = false;
		player.set_settings(settings);
		player.set_entries(std::move(entries));
	}

	std::unique_ptr<vr::Deck> make(const vr::PlaylistEntry &entry)
	{
		const float level = (float)std::stoi(entry.path.substr(3));
		auto deck = std::make_unique<FakeDeck>(level, scripts[entry.path]);
		created.push_back(entry.path);
		latest[entry.path] = deck.get();
		return deck;
	}

	/* Renders `ms` milliseconds and returns the left channel. Blocks that
	 * render() declined to output come back as NaN. */
	std::vector<float> run(int64_t ms)
	{
		std::vector<float> all;
		float left[BLOCK], right[BLOCK];
		float *out[2] = {left, right};

		for (int64_t done = 0; done < ms; done += (int64_t)BLOCK) {
			const bool output = player.render(out, BLOCK);
			for (size_t i = 0; i < BLOCK; i++)
				all.push_back(output ? left[i] : NAN);
		}

		return all;
	}

	vr::Snapshot snap()
	{
		vr::Snapshot s;
		player.fill_snapshot(s);
		return s;
	}

	vr::Player player;
	std::map<std::string, DeckScript> scripts;
	std::vector<std::string> created;
	std::map<std::string, FakeDeck *> latest; /* newest deck made for each track */
};

bool all_equal(const std::vector<float> &samples, size_t from, size_t to, float value)
{
	for (size_t i = from; i < to && i < samples.size(); i++) {
		if (std::isnan(value) ? !std::isnan(samples[i]) : samples[i] != value)
			return false;
	}
	return true;
}

} // namespace

int main()
{
	/* Plays the first track, with an exact sample clock. */
	{
		Rig rig(3);
		check_true("stopped renders nothing", std::isnan(rig.run(10)[0]));

		rig.player.play();
		const auto out = rig.run(1000);
		check_true("track 1 audible", all_equal(out, 0, 1000, 1.0f));
		check("index after play", rig.snap().index, 0);
		check("elapsed is exact", rig.snap().elapsed_ms, 1000);
		check("duration", rig.snap().duration_ms, 4000);
	}

	/* The bug this rewrite started from: seeking must stay on the track,
	 * even while the deck is still catching up with the new position. */
	{
		DeckScript script;
		script.seek_lag_reads = 5;
		Rig rig(3, script);
		rig.player.play();
		rig.run(500);

		const uint64_t serial = rig.snap().track_serial;
		rig.player.seek(2500);

		check("seek holds target while landing", rig.snap().elapsed_ms, 2500);
		const auto out = rig.run(500);
		check("seek stays on the track", rig.snap().index, 0);
		check("seek does not change track", (long long)rig.snap().track_serial, (long long)serial);
		check_true("silent while the deck lands", all_equal(out, 0, 50, 0.0f));
		check_true("audible after landing", out[499] == 1.0f);
		check("position after seek", rig.snap().elapsed_ms, 2950);
	}

	/* Seeking past the end ends the track normally. */
	{
		Rig rig(3);
		rig.player.play();
		rig.run(100);
		rig.player.seek(3990);
		rig.run(100);
		check("seek to the end moves on", rig.snap().index, 1);
	}

	/* Tracks run into each other at the exact sample. */
	{
		DeckScript script;
		script.duration_ms = 1005;
		Rig rig(3, script);
		rig.player.play();
		const auto out = rig.run(1010);
		check_true("gapless: end of track 1", all_equal(out, 995, 1005, 1.0f));
		check_true("gapless: start of track 2", all_equal(out, 1005, 1010, 2.0f));
		check("gapless: index", rig.snap().index, 1);
	}

	/* The next track is opened ahead of time. */
	{
		DeckScript script;
		script.duration_ms = 8000;
		Rig rig(3, script);
		rig.player.play();
		rig.run(2000);
		check("no prefetch early", (long long)rig.created.size(), 1);
		rig.run(1500);
		check("prefetched before the end", (long long)rig.created.size(), 2);
		rig.run(5000);
		check("prefetched deck is used", (long long)rig.created.size(), 2);
		check("index after prefetch", rig.snap().index, 1);
	}

	/* Repeat off stops after the last track. */
	{
		vr::PlayerSettings settings;
		settings.repeat = vr::RepeatMode::Off;
		DeckScript script;
		script.duration_ms = 100;
		Rig rig(2, script, settings);
		rig.player.play();
		const auto out = rig.run(300);
		check_true("repeat off: second track played", all_equal(out, 100, 200, 2.0f));
		check_true("repeat off: then nothing", std::isnan(out[250]));
		check("repeat off: stopped", (int)rig.player.transport(), (int)vr::PlayState::Stopped);
	}

	/* Repeat one replays, but an explicit next moves on. */
	{
		vr::PlayerSettings settings;
		settings.repeat = vr::RepeatMode::One;
		DeckScript script;
		script.duration_ms = 100;
		Rig rig(3, script, settings);
		rig.player.play();
		rig.run(250);
		check("repeat one replays", rig.snap().index, 0);
		rig.player.next();
		check("next skips under repeat one", rig.snap().index, 1);
	}

	/* Crossfade: both tracks audible, equal power, then only the new one. */
	{
		vr::PlayerSettings settings;
		settings.crossfade_ms = 1000;
		Rig rig(3, DeckScript{}, settings);
		rig.player.play();
		const auto out = rig.run(4500);
		check_true("before the crossfade", all_equal(out, 2900, 3000, 1.0f));
		check("crossfade starts at duration - fade", rig.snap().index, 1);
		const float middle = out[3500];
		check_near("crossfade mid-point mix", middle,
			   1.0 * std::cos(0.5 * 1.5707963) + 2.0 * std::sin(0.5 * 1.5707963), 0.01);
		check_true("after the crossfade", all_equal(out, 4010, 4500, 2.0f));
	}

	/* The last track of a non-repeating playlist is not faded into nothing. */
	{
		vr::PlayerSettings settings;
		settings.crossfade_ms = 1000;
		settings.repeat = vr::RepeatMode::Off;
		Rig rig(1, DeckScript{}, settings);
		rig.player.play();
		const auto out = rig.run(3990);
		check_true("last track plays to its end", all_equal(out, 3000, 3990, 1.0f));
	}

	/* A gap is silence on the same clock, then the next track. */
	{
		vr::PlayerSettings settings;
		settings.gap_ms = 500;
		DeckScript script;
		script.duration_ms = 1000;
		Rig rig(3, script, settings);
		rig.player.play();
		const auto out = rig.run(1600);
		check_true("gap is output as silence", all_equal(out, 1010, 1500, 0.0f));
		check_true("next track after the gap", all_equal(out, 1510, 1600, 2.0f));
	}

	/* Pause fades out, then outputs nothing; play fades back in. */
	{
		vr::PlayerSettings settings;
		settings.fade_out_ms = 200;
		settings.fade_in_ms = 100;
		Rig rig(2, DeckScript{}, settings);
		rig.player.play();
		rig.run(100);
		rig.player.pause();
		check("paused at once", (int)rig.player.transport(), (int)vr::PlayState::Paused);
		auto out = rig.run(300);
		check_true("fading out", out[0] > 0.9f && out[150] < 0.5f && out[150] > 0.0f);
		check_true("silent after the fade", std::isnan(out[250]));
		const int64_t paused_at = rig.snap().elapsed_ms;
		rig.run(500);
		check("position held while paused", rig.snap().elapsed_ms, paused_at);
		rig.player.play();
		out = rig.run(200);
		check_true("fading back in", out[0] < 0.2f && out[150] == 1.0f);
	}

	/* Stop with a fade stays playing until the fade is done. */
	{
		vr::PlayerSettings settings;
		settings.fade_out_ms = 200;
		Rig rig(2, DeckScript{}, settings);
		rig.player.play();
		rig.run(100);
		rig.player.stop();
		check("still playing during stop fade", (int)rig.player.transport(), (int)vr::PlayState::Playing);
		rig.run(300);
		check("stopped after the fade", (int)rig.player.transport(), (int)vr::PlayState::Stopped);
		rig.player.play();
		check("play after stop resumes the track", rig.snap().index, 0);
	}

	/* A deck still opening keeps the output going with silence. */
	{
		DeckScript script;
		script.open_reads = 3;
		Rig rig(2, script);
		rig.player.play();
		const auto out = rig.run(50);
		check_true("silence while opening", all_equal(out, 0, 30, 0.0f));
		check_true("audio once open", all_equal(out, 30, 50, 1.0f));
	}

	/* Unplayable tracks are skipped, but not forever. */
	{
		DeckScript script;
		script.fail = true;
		Rig rig(3, script);
		rig.scripts["/t/2"].fail = false;
		rig.player.play();
		rig.run(30);
		check("failed track is skipped", rig.snap().index, 1);
	}
	{
		DeckScript script;
		script.fail = true;
		Rig rig(3, script);
		rig.player.play();
		rig.run(200);
		check("all failing: stops", (int)rig.player.transport(), (int)vr::PlayState::Stopped);
		check("all failing: bounded attempts", (long long)rig.created.size(), 4);
	}
	{
		vr::PlayerSettings settings;
		settings.skip_on_error = false;
		DeckScript script;
		script.fail = true;
		Rig rig(3, script, settings);
		rig.player.play();
		rig.run(50);
		check("skip off: stops at once", (long long)rig.created.size(), 1);
	}

	/* Previous, play_index and restart. */
	{
		Rig rig(3);
		rig.player.play();
		rig.player.play_index(2);
		check("play_index", rig.snap().index, 2);
		rig.player.previous();
		check("previous", rig.snap().index, 1);
		rig.run(500);
		rig.player.restart();
		check("restart rewinds", rig.snap().elapsed_ms, 0);
		check("restart seeks the deck", rig.latest["/t/2"]->seeks, 1);
	}

	/* Live streams cannot seek. */
	{
		DeckScript script;
		script.seekable = false;
		script.report_duration = false;
		Rig rig(1, script);
		rig.player.play();
		rig.run(100);
		rig.player.seek(50);
		check("unseekable deck ignores seeks", rig.latest["/t/1"]->seeks, 0);
		check_true("snapshot says unseekable", !rig.snap().seekable);
	}

	/* Tags go to the right entry and the untagged queue moves on. */
	{
		Rig rig(3);
		rig.player.play();
		test::check_str("untagged: current first", rig.player.next_untagged(), "/t/1");
		vr::Tags tags;
		tags.title = "One";
		rig.player.set_tags("/t/1", tags);
		test::check_str("untagged: then next", rig.player.next_untagged(), "/t/2");
		test::check_str("tag shows in snapshot", rig.snap().title, "One");
	}

	/* Volume levelling: a track 6 dB louder than the target is turned down
	 * by 6 dB, from its first sample when its loudness is already known. */
	{
		vr::PlayerSettings settings;
		settings.level_mode = vr::LevelMode::Track;
		settings.level_target = -16.0f;
		Rig rig(2, {}, settings);

		vr::Tags tags;
		tags.track_loudness = -10.0f;
		rig.player.set_tags("/t/1", tags);
		rig.player.play();

		const auto out = rig.run(100);
		check_near("level: down 6 dB from the start", out[0], std::pow(10.0, -6.0 / 20.0), 1e-4);
		check_near("level: and stays there", out[99], std::pow(10.0, -6.0 / 20.0), 1e-4);
	}

	/* Loudness that turns up mid-track is ramped to, not jumped to. */
	{
		vr::PlayerSettings settings;
		settings.level_mode = vr::LevelMode::Track;
		settings.level_target = -16.0f;
		Rig rig(2, {}, settings);
		rig.player.play();
		rig.run(100);

		vr::Tags tags;
		tags.track_loudness = -4.0f; /* 12 dB too loud */
		rig.player.set_tags("/t/1", tags);

		const auto out = rig.run(1000);
		check_true("level: no jump", out[0] > 0.9f);
		check_true("level: on its way", out[200] < 0.9f && out[200] > 0.25f);
		check_near("level: arrives", out[900], std::pow(10.0, -12.0 / 20.0), 1e-3);
	}

	/* A quiet track is turned up, but never past its own peak. */
	{
		vr::PlayerSettings settings;
		settings.level_mode = vr::LevelMode::Track;
		settings.level_target = -16.0f;
		vr::Player probe({RATE, 2, 0}, nullptr);
		probe.set_settings(settings);

		vr::Tags quiet;
		quiet.track_loudness = -30.0f;
		check_near("level: quiet track up 14 dB", probe.level_for(quiet), std::pow(10.0, 14.0 / 20.0), 1e-3);

		quiet.track_peak = 0.5f;
		check_near("level: capped by the peak", probe.level_for(quiet), 2.0, 1e-4);

		settings.level_no_clip = false;
		probe.set_settings(settings);
		check_near("level: uncapped when asked", probe.level_for(quiet), std::pow(10.0, 14.0 / 20.0), 1e-3);

		/* Album mode takes the album's loudness when there is one. */
		settings.level_mode = vr::LevelMode::Album;
		probe.set_settings(settings);
		vr::Tags album;
		album.track_loudness = -10.0f;
		album.album_loudness = -16.0f;
		check_near("level: album loudness used", probe.level_for(album), 1.0, 1e-4);
		album.album_loudness = NAN;
		check_near("level: falls back to the track", probe.level_for(album), std::pow(10.0, -6.0 / 20.0), 1e-4);

		/* Unknown, or off: left alone. */
		check_near("level: unknown left alone", probe.level_for(vr::Tags{}), 1.0, 1e-6);
		settings.level_mode = vr::LevelMode::Off;
		probe.set_settings(settings);
		check_near("level: off", probe.level_for(album), 1.0, 1e-6);
	}

	/* Measuring is asked for once per file, and not while it is under way. */
	{
		vr::PlayerSettings settings;
		settings.level_mode = vr::LevelMode::Track;
		Rig rig(2, {}, settings);
		rig.player.play();

		vr::Tags tags;
		tags.title = "One";
		rig.player.set_tags("/t/1", tags);
		test::check_str("measure: still wanted", rig.player.next_untagged(), "/t/1");

		tags.measuring = true;
		rig.player.set_tags("/t/1", tags);
		test::check_str("measure: not while measuring", rig.player.next_untagged(), "/t/2");

		tags.measuring = false;
		tags.loudness_checked = true;
		rig.player.set_tags("/t/1", tags);
		test::check_str("measure: done once tried", rig.player.next_untagged(), "/t/2");

		settings.level_measure = false;
		rig.player.set_settings(settings);
		vr::Tags plain;
		rig.player.set_tags("/t/2", plain);
		test::check_str("measure: off, nothing more to do", rig.player.next_untagged(), "");
	}

	/* Skipping silence: two seconds of it at the start are gone at once. */
	{
		DeckScript script;
		script.lead_silence_ms = 2000;
		vr::PlayerSettings settings;
		settings.trim_silence = true;
		Rig rig(2, script, settings);
		rig.player.play();

		const auto out = rig.run(50);
		check_true("trim: sound within the first block", out[0] == 1.0f || out[9] == 1.0f);
		check_true("trim: and from then on", all_equal(out, 10, 50, 1.0f));
		check_true("trim: deck moved past the silence", rig.latest["/t/1"]->position_ms() > 2000);
	}

	/* Without it, the silence plays. */
	{
		DeckScript script;
		script.lead_silence_ms = 2000;
		Rig rig(2, script);
		rig.player.play();
		const auto out = rig.run(2100);
		check_true("no trim: silence plays", all_equal(out, 0, 2000, 0.0f));
		check_true("no trim: then the track", all_equal(out, 2000, 2100, 1.0f));
	}

	/* A track whose last 2.5 s are silent ends after 1.5 s of it. */
	{
		DeckScript script;
		script.tail_silence_ms = 2500;
		vr::PlayerSettings settings;
		settings.trim_silence = true;
		Rig rig(2, script, settings);
		rig.player.play();

		const auto out = rig.run(3500);
		/* Sound to 1500, silence to 3000, then track 2. */
		check_true("tail: plays to the silence", all_equal(out, 0, 1500, 1.0f));
		check_true("tail: next track by 3000 ms", all_equal(out, 3010, 3500, 2.0f));
		check("tail: moved on", rig.snap().index, 1);
	}

	/* A seek into silence lands there: nothing is skipped. */
	{
		DeckScript script;
		script.lead_silence_ms = 2000;
		vr::PlayerSettings settings;
		settings.trim_silence = true;
		Rig rig(2, script, settings);
		rig.player.play();
		rig.run(100);
		rig.player.seek(500);
		const auto out = rig.run(300);
		check_true("seek into silence: stays silent", all_equal(out, 20, 300, 0.0f));
	}

	/* A long silent stretch in the middle of a track is left alone. */
	{
		DeckScript script;
		script.duration_ms = 60000;
		script.lead_silence_ms = 0;
		vr::PlayerSettings settings;
		settings.trim_silence = true;
		Rig rig(2, script, settings);
		rig.player.play();
		rig.run(20000);
		check("middle: still on track 1", rig.snap().index, 0);
	}

	return test::finish();
}
