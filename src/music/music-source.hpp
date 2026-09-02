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

#include "music/music-api.hpp"
#include "music/playlist.hpp"

#include <obs-module.h>
#include <media-io/audio-io.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vr {

/*
 * A playlist driven audio source.
 *
 * Decoding is delegated to private `ffmpeg_source` children ("decks"), one per
 * track being played. Their audio is captured, gain staged and mixed back out
 * through this source, which means the plugin only has to own playlist and
 * transport logic while OBS keeps handling formats, seeking and timing. Mixing
 * in this source (rather than letting OBS mix the children directly) is what
 * lets the music land in the mixer as one channel with working volume, filters
 * and monitoring.
 *
 * Two decks are alive at once during a crossfade: the outgoing deck keeps
 * driving the output while the incoming one buffers, and the roles swap when
 * the outgoing deck runs dry.
 */
class MusicSource {
public:
	static void register_type();
	static MusicSource *from_source(obs_source_t *source);

	MusicSource(obs_data_t *settings, obs_source_t *source);
	~MusicSource();

	MusicSource(const MusicSource &) = delete;
	MusicSource &operator=(const MusicSource &) = delete;

	void update(obs_data_t *settings);
	void activate();
	void deactivate();

	bool snapshot(Snapshot &out);
	void entries(std::vector<EntryInfo> &out, int &current_index);

	void play();
	void pause();
	void toggle_pause();
	void stop();
	void next();
	void previous();
	void restart();
	void seek(int64_t ms);
	void play_index(int index);

	int64_t media_time();
	int64_t media_duration();
	obs_media_state media_state();

	void set_shuffle(bool shuffle);
	void set_repeat(RepeatMode mode);
	void set_override(const std::string &path, const std::string &title, const std::string &artist);
	void set_order(const std::vector<std::string> &paths);
	void remove_path(const std::string &path);

private:
	/* Single producer, single consumer float FIFO used to hold a deck's
	 * audio while another deck is driving the output. */
	struct Ring {
		std::vector<float> data;
		size_t head = 0;
		size_t size = 0;

		void reset(size_t capacity);
		void clear();
		void push(const float *src, size_t count);
		size_t pop(float *dst, size_t count);
	};

	struct Deck {
		MusicSource *owner = nullptr;
		obs_source_t *media = nullptr;
		int entry_index = -1;
		bool callback_added = false;

		std::atomic<bool> driver{false};
		std::atomic<bool> produced{false};
		std::atomic<bool> drain_backlog{false};

		/* Set once the deck reaching its end has been acted on, so that
		 * a fade out running afterwards is not restarted on every poll
		 * for as long as the finished deck is still around. */
		std::atomic<bool> finish_handled{false};

		std::mutex ring_mutex;
		Ring rings[MAX_AUDIO_CHANNELS];
		size_t ring_channels = 0;

		std::mutex env_mutex;
		double gain_from = 1.0;
		double gain_to = 1.0;
		uint64_t env_pos = 0;
		uint64_t env_len = 0;
		bool equal_power = false;

		/* Wall clock deadline after which the deck is torn down, used
		 * to retire the outgoing half of a crossfade. */
		uint64_t expire_ns = 0;
		uint64_t created_ns = 0;

		std::vector<float> scratch[MAX_AUDIO_CHANNELS];
		std::vector<float> drain[MAX_AUDIO_CHANNELS];
		std::vector<float> mix_tmp;

		void set_gain(double gain);
		void fade_to(double target, uint64_t samples, bool equal);
		void apply_gain(const float *const *in, float *const *out, size_t frames, size_t channels, bool muted);
	};

	using DeckPtr = std::shared_ptr<Deck>;

	struct HotkeyContext;

	static void audio_capture(void *param, obs_source_t *source, const struct audio_data *audio, bool muted);
	void on_deck_audio(Deck *deck, const struct audio_data *audio, bool muted);

	void housekeeping_thread();
	void tick();
	void wake();
	void poll_playback();
	void publish_snapshot();

	void rebuild_playlist();
	void load_pending_tags();

	DeckPtr create_deck(int entry_index);
	void start_entry(int entry_index, int crossfade_ms);
	void destroy_deck(DeckPtr &deck);
	void retire_all_decks();
	void reap_decks();
	static bool deck_finished(const Deck &deck);
	DeckPtr current_deck();

	void advance_track(int crossfade_ms, bool user_initiated);
	void note_track_change(int entry_index);
	int manual_crossfade_ms();

	obs_data_t *settings();
	void write_setting_bool(const char *name, bool value);
	void write_setting_int(const char *name, int64_t value);

	static void hotkey_pressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	void register_hotkeys();
	void unregister_hotkeys();

	obs_source_t *self_ = nullptr;

	/* Guards playlist, transport and playback bookkeeping. Lock order is
	 * always state_mutex_ before decks_mutex_. */
	std::mutex state_mutex_;
	Playlist playlist_;
	PlayState transport_ = PlayState::Stopped;
	uint64_t track_serial_ = 0;

	int64_t last_media_ms_ = 0;
	uint64_t last_media_ns_ = 0;
	int64_t duration_ms_ = 0;
	bool time_reset_ = true;
	bool crossfade_active_ = false;
	int consecutive_errors_ = 0;

	/* A seek takes a moment to land, and a decoder in the middle of one can
	 * report both a stale position and, briefly, a stopped state. Until the
	 * deadline passes, the requested position is what the plugin believes
	 * and the deck is not allowed to count as finished. */
	int64_t seek_target_ms_ = 0;
	uint64_t seek_settle_ns_ = 0;

	int pending_entry_ = -1;
	uint64_t pending_start_ns_ = 0;
	uint64_t pending_pause_ns_ = 0;
	uint64_t pending_stop_ns_ = 0;

	bool rebuild_pending_ = true;
	bool autostart_pending_ = false;
	bool paused_by_inactive_ = false;

	/* Settings mirror. */
	std::vector<std::string> configured_paths_;
	std::vector<PlaylistEntry> overrides_;
	bool recurse_dirs_ = true;
	bool shuffle_ = false;
	RepeatMode repeat_ = RepeatMode::All;
	int crossfade_ms_ = 0;
	int gap_ms_ = 0;
	int fade_in_ms_ = 0;
	int fade_out_ms_ = 0;
	bool play_on_start_ = false;
	bool keep_playing_when_hidden_ = true;
	bool skip_on_error_ = true;

	std::mutex decks_mutex_;
	std::vector<DeckPtr> decks_;
	DeckPtr current_;

	/* Read by the graphics thread every frame, so it is deliberately kept
	 * behind its own short-lived lock rather than the state mutex. */
	std::mutex snapshot_mutex_;
	Snapshot cached_;
	uint64_t cached_ns_ = 0;

	std::atomic<bool> resume_on_tick_{false};

	std::thread thread_;
	std::condition_variable cv_;
	std::atomic<bool> stop_thread_{false};
	bool wake_flag_ = false;

	obs_hotkey_id hotkey_play_pause_ = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkey_stop_ = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkey_next_ = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkey_previous_ = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkey_restart_ = OBS_INVALID_HOTKEY_ID;
	std::vector<HotkeyContext *> hotkey_contexts_;
};

} // namespace vr
