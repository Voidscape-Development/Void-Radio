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

#include "music/command-queue.hpp"
#include "music/ducker.hpp"
#include "music/library-worker.hpp"
#include "music/music-api.hpp"
#include "music/player.hpp"
#include "music/source-hotkeys.hpp"

#include <obs-module.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vr {

void register_music_source();

/*
 * The "Music" source: the OBS side of the playback engine.
 *
 * Threads, and what each one owns:
 *
 *  - The engine thread owns the Player and everything in it. It wakes once per
 *    block of audio, runs whatever commands have been posted, renders the
 *    block, sends it to OBS and publishes the state that everyone else reads.
 *  - The library worker walks folders and reads tags, and posts what it finds
 *    back to the engine as commands.
 *  - Each track's deck decodes on a thread of its own (see StreamDeck).
 *  - Everything else - OBS callbacks, hotkeys, the dock, other sources - only
 *    ever posts commands or reads the published snapshot and entry list.
 *
 * Output timestamps are counted in samples from when playback started, so the
 * audio OBS receives is exactly continuous however the thread is scheduled.
 */
class MusicSource {
public:
	static MusicSource *from_source(obs_source_t *source);

	MusicSource(obs_data_t *settings, obs_source_t *source);
	~MusicSource();

	MusicSource(const MusicSource &) = delete;
	MusicSource &operator=(const MusicSource &) = delete;

	/* OBS callbacks. */
	void update(obs_data_t *settings);
	void activate();
	void deactivate();

	/* Transport. These post to the engine and return at once. */
	void play();
	void pause();
	void toggle_pause();
	void stop();
	void next();
	void previous();
	void restart();
	void seek(int64_t ms);
	void play_index(int index);

	void queue_index(int index, bool next);
	void unqueue(int position);
	void move_queued(int from, int to);
	void clear_queue();

	/* Published state. Cheap, and safe from any thread. */
	bool snapshot(Snapshot &out) const;
	void entries(std::vector<EntryInfo> &out, int &current_index) const;
	void queue(std::vector<int> &out) const;
	int64_t media_time() const;
	int64_t media_duration() const;
	obs_media_state media_state() const;

	/* Changes made from the dock, written through to the source settings. */
	void set_shuffle(bool shuffle);
	void set_repeat(RepeatMode mode);
	void set_override(const std::string &path, const std::string &title, const std::string &artist);
	void set_order(const std::vector<std::string> &paths);
	void remove_path(const std::string &path);

	obs_source_t *source() const { return self_; }

	/* Whether the music is being turned down for the duck trigger right
	 * now, and by how much, in decibels. For display. */
	float duck_gain_db() const { return duck_db_.load(); }

	/* Fills a property list with the sources that can trigger ducking. */
	static void list_duck_sources(obs_property_t *list, obs_source_t *self);

private:
	/* Engine thread. */
	void engine_thread();
	void output_block(size_t frames);
	void publish();
	void request_tags();

	void post(CommandQueue::Command command) { commands_.post(std::move(command)); }

	/* Ducking. The trigger is bound by name, and rebound from the engine
	 * thread if it is renamed, deleted or loaded after this source. */
	void bind_duck_trigger(const std::string &name);
	void unbind_duck_trigger();
	void retry_duck_trigger();
	void apply_duck(float *const *planes, size_t frames);
	static void duck_capture(void *param, obs_source_t *source, const struct audio_data *audio, bool muted);

	void write_setting_bool(const char *name, bool value);
	void write_setting_int(const char *name, int64_t value);

	obs_source_t *self_ = nullptr;
	AudioFormat format_;
	enum speaker_layout speakers_ = SPEAKERS_STEREO;

	CommandQueue commands_;

	/* Owned by the engine thread once it is running. */
	std::unique_ptr<Player> player_;
	bool autostart_pending_ = false;
	bool keep_playing_when_hidden_ = true;
	bool paused_by_inactive_ = false;
	std::string tags_in_flight_;
	uint64_t published_entries_version_ = UINT64_MAX;
	std::vector<float> block_[MAX_MIX_CHANNELS];

	/* Output timeline: when the current run of audio started, and how many
	 * samples have gone out since. Reset whenever output stops. */
	bool timeline_running_ = false;
	uint64_t timeline_start_ns_ = 0;
	uint64_t timeline_frames_ = 0;

	/* Last settings seen, to tell what actually changed. Touched only
	 * from OBS's update callback. */
	PlaylistConfig playlist_config_;
	bool have_config_ = false;

	/* Published by the engine thread. */
	mutable std::mutex snapshot_mutex_;
	Snapshot snapshot_;
	uint64_t snapshot_ns_ = 0;

	mutable std::mutex entries_mutex_;
	std::vector<EntryInfo> entries_;
	int current_index_ = -1;
	std::vector<int> queue_;

	std::unique_ptr<LibraryWorker> library_;

	std::atomic<bool> running_{false};
	std::thread thread_;

	std::unique_ptr<SourceHotkeys> hotkeys_;

	/* Ducking: the binding is shared between the UI and engine threads;
	 * the peak is written on OBS's audio thread and taken by the engine. */
	std::mutex duck_mutex_;
	std::string duck_name_;
	obs_weak_source_t *duck_weak_ = nullptr;
	std::atomic<float> duck_peak_{0.0f};
	std::atomic<float> duck_db_{0.0f};

	/* Engine thread. */
	Ducker ducker_;
	float duck_gain_ = 1.0f;
	uint64_t duck_retry_frames_ = 0;
};

} // namespace vr
