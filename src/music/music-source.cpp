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

#include "music/music-source.hpp"
#include "util/vr-util.hpp"

#include <plugin-support.h>
#include <util/platform.h>
#include <util/threading.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace vr {

namespace {

constexpr uint64_t TICK_INTERVAL_MS = 20;
constexpr size_t MAX_MIX_DECKS = 3;
constexpr int MAX_TAG_LOADS_PER_TICK = 6;

/* Audio buffered for a deck that is not currently driving the output. Two
 * seconds is far more than a crossfade needs and bounds the memory. */
constexpr double RING_SECONDS = 2.0;

/* Spelled out rather than taken from <cmath>, which does not define M_PI and
 * friends on every toolchain. */
constexpr double HALF_PI = 1.57079632679489661923;

uint64_t ms_to_samples(int64_t ms, size_t rate)
{
	if (ms <= 0)
		return 0;

	return (uint64_t)((double)ms * (double)rate / 1000.0);
}

double curve(double t, bool equal_power, bool rising)
{
	if (t <= 0.0)
		return 0.0;
	if (t >= 1.0)
		return 1.0;

	if (!equal_power)
		return t;

	/* sin/cos pair keeps the summed power of a crossfade constant. */
	return rising ? std::sin(t * HALF_PI) : 1.0 - std::cos(t * HALF_PI);
}

} // namespace

/* -------------------------------------------------------------------------- */
/* Ring                                                                        */
/* -------------------------------------------------------------------------- */

void MusicSource::Ring::reset(size_t capacity)
{
	data.assign(capacity, 0.0f);
	head = 0;
	size = 0;
}

void MusicSource::Ring::clear()
{
	head = 0;
	size = 0;
}

void MusicSource::Ring::push(const float *src, size_t count)
{
	const size_t capacity = data.size();
	if (!capacity || !count)
		return;

	if (count > capacity) {
		src += count - capacity;
		count = capacity;
	}

	if (size + count > capacity) {
		const size_t drop = size + count - capacity;
		head = (head + drop) % capacity;
		size -= drop;
	}

	const size_t tail = (head + size) % capacity;
	const size_t first = std::min(count, capacity - tail);

	memcpy(&data[tail], src, first * sizeof(float));
	if (count > first)
		memcpy(&data[0], src + first, (count - first) * sizeof(float));

	size += count;
}

size_t MusicSource::Ring::pop(float *dst, size_t count)
{
	const size_t capacity = data.size();
	if (!capacity)
		return 0;

	const size_t take = std::min(count, size);
	if (!take)
		return 0;

	const size_t first = std::min(take, capacity - head);

	memcpy(dst, &data[head], first * sizeof(float));
	if (take > first)
		memcpy(dst + first, &data[0], (take - first) * sizeof(float));

	head = (head + take) % capacity;
	size -= take;

	return take;
}

/* -------------------------------------------------------------------------- */
/* Deck                                                                        */
/* -------------------------------------------------------------------------- */

void MusicSource::Deck::set_gain(double gain)
{
	std::lock_guard<std::mutex> lock(env_mutex);
	gain_from = gain;
	gain_to = gain;
	env_pos = 0;
	env_len = 0;
}

void MusicSource::Deck::fade_to(double target, uint64_t samples, bool equal)
{
	std::lock_guard<std::mutex> lock(env_mutex);

	/* Start from wherever the running envelope currently sits. */
	double current = gain_to;
	if (env_len && env_pos < env_len) {
		const double t = (double)env_pos / (double)env_len;
		current = gain_from + (gain_to - gain_from) * curve(t, equal_power, gain_to > gain_from);
	}

	gain_from = current;
	gain_to = target;
	env_pos = 0;
	env_len = samples;
	equal_power = equal;

	if (!samples)
		gain_from = target;
}

void MusicSource::Deck::apply_gain(const float *const *in, float *const *out, size_t frames, size_t channels,
				   bool muted)
{
	std::lock_guard<std::mutex> lock(env_mutex);

	const bool rising = gain_to > gain_from;
	const double span = gain_to - gain_from;
	const float mute_scale = muted ? 0.0f : 1.0f;

	for (size_t i = 0; i < frames; i++) {
		double gain = gain_to;

		if (env_len && env_pos < env_len) {
			const double t = (double)env_pos / (double)env_len;
			gain = gain_from + span * curve(t, equal_power, rising);
			env_pos++;
		} else if (env_len) {
			gain_from = gain_to;
			env_len = 0;
			env_pos = 0;
		}

		const float scaled = (float)gain * mute_scale;

		for (size_t ch = 0; ch < channels; ch++)
			out[ch][i] = in[ch] ? in[ch][i] * scaled : 0.0f;
	}
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */
/* -------------------------------------------------------------------------- */

MusicSource::MusicSource(obs_data_t *settings, obs_source_t *source) : self_(source)
{
	update(settings);
	register_hotkeys();

	autostart_pending_ = play_on_start_;

	thread_ = std::thread([this]() { housekeeping_thread(); });
}

MusicSource::~MusicSource()
{
	stop_thread_.store(true);
	wake();

	if (thread_.joinable())
		thread_.join();

	unregister_hotkeys();
	retire_all_decks();
}

MusicSource *MusicSource::from_source(obs_source_t *source)
{
	if (!source)
		return nullptr;

	const char *id = obs_source_get_unversioned_id(source);
	if (!id || strcmp(id, MUSIC_SOURCE_ID) != 0)
		return nullptr;

	return static_cast<MusicSource *>(obs_obj_get_data(source));
}

/* -------------------------------------------------------------------------- */
/* Settings                                                                    */
/* -------------------------------------------------------------------------- */

void MusicSource::update(obs_data_t *settings)
{
	std::vector<std::string> paths;

	obs_data_array_t *array = obs_data_get_array(settings, "playlist");
	if (array) {
		const size_t count = obs_data_array_count(array);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(array, i);
			const char *value = obs_data_get_string(item, "value");

			if (value && *value)
				paths.emplace_back(value);

			obs_data_release(item);
		}
		obs_data_array_release(array);
	}

	std::vector<PlaylistEntry> overrides;

	obs_data_array_t *override_array = obs_data_get_array(settings, "overrides");
	if (override_array) {
		const size_t count = obs_data_array_count(override_array);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(override_array, i);

			PlaylistEntry entry;
			entry.path = obs_data_get_string(item, "path");
			entry.override_title = obs_data_get_string(item, "title");
			entry.override_artist = obs_data_get_string(item, "artist");

			if (!entry.path.empty())
				overrides.push_back(std::move(entry));

			obs_data_release(item);
		}
		obs_data_array_release(override_array);
	}

	std::lock_guard<std::mutex> lock(state_mutex_);

	const bool recurse = obs_data_get_bool(settings, "recurse_dirs");

	bool overrides_changed = overrides.size() != overrides_.size();
	for (size_t i = 0; !overrides_changed && i < overrides.size(); i++) {
		overrides_changed = overrides[i].path != overrides_[i].path ||
				    overrides[i].override_title != overrides_[i].override_title ||
				    overrides[i].override_artist != overrides_[i].override_artist;
	}

	if (paths != configured_paths_ || recurse != recurse_dirs_ || overrides_changed) {
		configured_paths_ = paths;
		recurse_dirs_ = recurse;
		rebuild_pending_ = true;
	}

	overrides_ = std::move(overrides);

	const bool shuffle = obs_data_get_bool(settings, "shuffle");
	if (shuffle != shuffle_) {
		shuffle_ = shuffle;
		playlist_.set_shuffle(shuffle);
	}

	repeat_ = (RepeatMode)obs_data_get_int(settings, "repeat");
	crossfade_ms_ = (int)obs_data_get_int(settings, "crossfade_ms");
	gap_ms_ = (int)obs_data_get_int(settings, "gap_ms");
	fade_in_ms_ = (int)obs_data_get_int(settings, "fade_in_ms");
	fade_out_ms_ = (int)obs_data_get_int(settings, "fade_out_ms");
	play_on_start_ = obs_data_get_bool(settings, "play_on_start");
	keep_playing_when_hidden_ = obs_data_get_bool(settings, "keep_playing_when_hidden");
	skip_on_error_ = obs_data_get_bool(settings, "skip_on_error");

	wake_flag_ = true;
	cv_.notify_all();
}

obs_data_t *MusicSource::settings()
{
	return obs_source_get_settings(self_);
}

void MusicSource::write_setting_bool(const char *name, bool value)
{
	obs_data_t *data = settings();
	obs_data_set_bool(data, name, value);
	obs_source_update(self_, data);
	obs_data_release(data);
}

void MusicSource::write_setting_int(const char *name, int64_t value)
{
	obs_data_t *data = settings();
	obs_data_set_int(data, name, value);
	obs_source_update(self_, data);
	obs_data_release(data);
}

/* -------------------------------------------------------------------------- */
/* Playlist                                                                    */
/* -------------------------------------------------------------------------- */

void MusicSource::rebuild_playlist()
{
	std::vector<std::string> configured;
	std::vector<PlaylistEntry> overrides;
	bool recurse;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		configured = configured_paths_;
		overrides = overrides_;
		recurse = recurse_dirs_;
		rebuild_pending_ = false;
	}

	/* Directory scanning touches the disk, so it happens outside the lock. */
	const std::vector<std::string> expanded = expand_paths(configured, recurse);

	std::vector<PlaylistEntry> entries;
	entries.reserve(expanded.size());

	for (const std::string &path : expanded) {
		PlaylistEntry entry;
		entry.path = path;
		entry.url = is_url(path);
		entry.missing = !entry.url && !os_file_exists(path.c_str());

		for (const PlaylistEntry &override_entry : overrides) {
			if (override_entry.path == path) {
				entry.override_title = override_entry.override_title;
				entry.override_artist = override_entry.override_artist;
				break;
			}
		}

		entries.push_back(std::move(entry));
	}

	std::lock_guard<std::mutex> lock(state_mutex_);

	/* Carry already-read tags across a rebuild so the dock does not flicker. */
	for (PlaylistEntry &entry : entries) {
		const PlaylistEntry *existing = playlist_.at(playlist_.index_of(entry.path));
		if (existing && existing->tags_loaded) {
			entry.tags = existing->tags;
			entry.tags_loaded = true;
		}
	}

	playlist_.set_entries(std::move(entries));
	playlist_.set_shuffle(shuffle_);
}

void MusicSource::load_pending_tags()
{
	for (int loaded = 0; loaded < MAX_TAG_LOADS_PER_TICK; loaded++) {
		std::string path;

		{
			std::lock_guard<std::mutex> lock(state_mutex_);

			/* The playing track and the one after it come first so the
			 * info filter has something to show immediately. */
			const int candidates[] = {playlist_.current(), playlist_.peek_next(repeat_)};
			int target = -1;

			for (int candidate : candidates) {
				const PlaylistEntry *entry = playlist_.at(candidate);
				if (entry && !entry->tags_loaded && !entry->url) {
					target = candidate;
					break;
				}
			}

			if (target < 0) {
				const std::vector<PlaylistEntry> &entries = playlist_.entries();
				for (size_t i = 0; i < entries.size(); i++) {
					if (!entries[i].tags_loaded && !entries[i].url) {
						target = (int)i;
						break;
					}
				}
			}

			if (target < 0)
				return;

			path = playlist_.at(target)->path;
		}

		Tags tags;
		read_tags(path, tags);

		{
			std::lock_guard<std::mutex> lock(state_mutex_);
			PlaylistEntry *entry = playlist_.at(playlist_.index_of(path));
			if (entry) {
				entry->tags = std::move(tags);
				entry->tags_loaded = true;
			}
		}
	}
}

/* -------------------------------------------------------------------------- */
/* Decks                                                                       */
/* -------------------------------------------------------------------------- */

void MusicSource::audio_capture(void *param, obs_source_t *source, const struct audio_data *audio, bool muted)
{
	UNUSED_PARAMETER(source);

	Deck *deck = static_cast<Deck *>(param);
	if (deck && deck->owner)
		deck->owner->on_deck_audio(deck, audio, muted);
}

void MusicSource::on_deck_audio(Deck *deck, const struct audio_data *audio, bool muted)
{
	const size_t frames = audio->frames;
	if (!frames)
		return;

	audio_t *obs_audio = obs_get_audio();
	if (!obs_audio)
		return;

	const size_t channels = audio_output_get_channels(obs_audio);
	const size_t sample_rate = audio_output_get_sample_rate(obs_audio);

	if (!channels || channels > MAX_AUDIO_CHANNELS)
		return;

	const float *in[MAX_AUDIO_CHANNELS] = {};
	float *out[MAX_AUDIO_CHANNELS] = {};

	for (size_t ch = 0; ch < channels; ch++) {
		in[ch] = (const float *)audio->data[ch];
		deck->scratch[ch].resize(frames);
		out[ch] = deck->scratch[ch].data();
	}

	deck->apply_gain(in, out, frames, channels, muted);
	deck->produced.store(true);

	if (!deck->driver.load()) {
		std::lock_guard<std::mutex> lock(deck->ring_mutex);

		if (deck->ring_channels != channels) {
			const size_t capacity = (size_t)(RING_SECONDS * (double)sample_rate);
			for (size_t ch = 0; ch < channels; ch++)
				deck->rings[ch].reset(capacity);
			deck->ring_channels = channels;
		}

		for (size_t ch = 0; ch < channels; ch++)
			deck->rings[ch].push(out[ch], frames);

		return;
	}

	/* Just promoted to driver: whatever this deck buffered while the other
	 * half of the crossfade was driving has to go out first, otherwise the
	 * handover punches a hole in the timeline. */
	uint64_t timestamp = os_gettime_ns();

	if (deck->drain_backlog.exchange(false)) {
		size_t drained = 0;

		{
			std::lock_guard<std::mutex> lock(deck->ring_mutex);

			if (deck->ring_channels == channels) {
				drained = deck->rings[0].size;

				for (size_t ch = 0; ch < channels; ch++) {
					deck->drain[ch].resize(drained);
					deck->rings[ch].pop(deck->drain[ch].data(), drained);
				}
			}
		}

		if (drained) {
			struct obs_source_audio backlog = {};
			for (size_t ch = 0; ch < channels; ch++)
				backlog.data[ch] = (const uint8_t *)deck->drain[ch].data();

			backlog.frames = (uint32_t)drained;
			backlog.speakers = audio_output_get_info(obs_audio)->speakers;
			backlog.format = AUDIO_FORMAT_FLOAT_PLANAR;
			backlog.samples_per_sec = (uint32_t)sample_rate;
			backlog.timestamp = timestamp;

			obs_source_output_audio(self_, &backlog);

			timestamp += (uint64_t)drained * 1000000000ULL / (uint64_t)sample_rate;
		}
	}

	/* Fold in whatever the other half of a crossfade has buffered. */
	DeckPtr others[MAX_MIX_DECKS];
	size_t other_count = 0;

	{
		std::lock_guard<std::mutex> lock(decks_mutex_);
		for (const DeckPtr &other : decks_) {
			if (other.get() == deck || other_count >= MAX_MIX_DECKS)
				continue;
			others[other_count++] = other;
		}
	}

	deck->mix_tmp.resize(frames);

	for (size_t i = 0; i < other_count; i++) {
		Deck *other = others[i].get();
		std::lock_guard<std::mutex> lock(other->ring_mutex);

		if (other->ring_channels != channels)
			continue;

		const size_t available = other->rings[0].size;
		const size_t take = std::min(available, frames);
		if (!take)
			continue;

		for (size_t ch = 0; ch < channels; ch++) {
			const size_t popped = other->rings[ch].pop(deck->mix_tmp.data(), take);
			for (size_t j = 0; j < popped; j++)
				out[ch][j] += deck->mix_tmp[j];
		}
	}

	struct obs_source_audio output = {};
	for (size_t ch = 0; ch < channels; ch++)
		output.data[ch] = (const uint8_t *)out[ch];

	output.frames = (uint32_t)frames;
	output.speakers = audio_output_get_info(obs_audio)->speakers;
	output.format = AUDIO_FORMAT_FLOAT_PLANAR;
	output.samples_per_sec = (uint32_t)sample_rate;
	output.timestamp = timestamp;

	obs_source_output_audio(self_, &output);
}

MusicSource::DeckPtr MusicSource::create_deck(int entry_index)
{
	std::string path;
	bool url = false;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		const PlaylistEntry *entry = playlist_.at(entry_index);
		if (!entry)
			return nullptr;

		path = entry->path;
		url = entry->url;
	}

	obs_data_t *media_settings = obs_data_create();

	if (url) {
		obs_data_set_bool(media_settings, "is_local_file", false);
		obs_data_set_string(media_settings, "input", path.c_str());
		obs_data_set_int(media_settings, "reconnect_delay_sec", 5);
	} else {
		obs_data_set_bool(media_settings, "is_local_file", true);
		obs_data_set_string(media_settings, "local_file", path.c_str());
	}

	obs_data_set_bool(media_settings, "looping", false);
	obs_data_set_bool(media_settings, "clear_on_media_end", true);
	obs_data_set_bool(media_settings, "hw_decode", false);
	obs_data_set_bool(media_settings, "seekable", true);

	/* The deck must keep decoding no matter what the parent's scene state is;
	 * pausing on hide is handled by this plugin instead. */
	obs_data_set_bool(media_settings, "close_when_inactive", false);
	obs_data_set_bool(media_settings, "restart_on_activate", false);

	obs_source_t *media = obs_source_create_private("ffmpeg_source", "Void Radio Deck", media_settings);
	obs_data_release(media_settings);

	if (!media) {
		obs_log(LOG_WARNING, "failed to create playback deck for '%s'", path.c_str());
		return nullptr;
	}

	DeckPtr deck = std::make_shared<Deck>();
	deck->owner = this;
	deck->media = media;
	deck->entry_index = entry_index;
	deck->created_ns = os_gettime_ns();

	return deck;
}

void MusicSource::start_entry(int entry_index, int crossfade_ms)
{
	DeckPtr deck = create_deck(entry_index);
	if (!deck) {
		std::lock_guard<std::mutex> lock(state_mutex_);
		consecutive_errors_++;
		return;
	}

	audio_t *obs_audio = obs_get_audio();
	const size_t sample_rate = obs_audio ? audio_output_get_sample_rate(obs_audio) : 48000;

	int fade_in_ms;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		fade_in_ms = fade_in_ms_;
	}

	const bool crossfading = crossfade_ms > 0;

	if (crossfading) {
		deck->set_gain(0.0);
		deck->fade_to(1.0, ms_to_samples(crossfade_ms, sample_rate), true);
	} else if (fade_in_ms > 0) {
		deck->set_gain(0.0);
		deck->fade_to(1.0, ms_to_samples(fade_in_ms, sample_rate), false);
	} else {
		deck->set_gain(1.0);
	}

	std::vector<DeckPtr> dead;

	{
		std::lock_guard<std::mutex> lock(decks_mutex_);

		const uint64_t now = os_gettime_ns();

		for (const DeckPtr &existing : decks_) {
			if (crossfading) {
				existing->fade_to(0.0, ms_to_samples(crossfade_ms, sample_rate), true);
				existing->expire_ns = now + (uint64_t)crossfade_ms * 1000000ULL;
			} else {
				existing->expire_ns = now;
			}
		}

		if (!crossfading) {
			/* Nothing to blend with, so the new deck drives at once. */
			for (const DeckPtr &existing : decks_)
				existing->driver.store(false);
			deck->driver.store(true);
		} else {
			deck->driver.store(false);
		}

		decks_.push_back(deck);
		current_ = deck;

		if (!crossfading) {
			for (auto it = decks_.begin(); it != decks_.end();) {
				if (*it != deck) {
					dead.push_back(*it);
					it = decks_.erase(it);
				} else {
					++it;
				}
			}
		}
	}

	for (DeckPtr &old_deck : dead)
		destroy_deck(old_deck);

	obs_source_add_audio_capture_callback(deck->media, audio_capture, deck.get());
	deck->callback_added = true;

	note_track_change(entry_index);
}

void MusicSource::destroy_deck(DeckPtr &deck)
{
	if (!deck)
		return;

	if (deck->callback_added) {
		obs_source_remove_audio_capture_callback(deck->media, audio_capture, deck.get());
		deck->callback_added = false;
	}

	if (deck->media) {
		obs_source_media_stop(deck->media);
		obs_source_release(deck->media);
		deck->media = nullptr;
	}

	deck.reset();
}

void MusicSource::retire_all_decks()
{
	std::vector<DeckPtr> dead;

	{
		std::lock_guard<std::mutex> lock(decks_mutex_);
		dead.swap(decks_);
		current_.reset();
	}

	for (DeckPtr &deck : dead)
		destroy_deck(deck);
}

void MusicSource::reap_decks()
{
	std::vector<DeckPtr> dead;
	bool promote = false;

	{
		std::lock_guard<std::mutex> lock(decks_mutex_);
		const uint64_t now = os_gettime_ns();

		for (auto it = decks_.begin(); it != decks_.end();) {
			const DeckPtr &deck = *it;

			if (deck == current_) {
				++it;
				continue;
			}

			const bool expired = deck->expire_ns && now >= deck->expire_ns;

			if (expired || deck_finished(*deck)) {
				dead.push_back(deck);
				it = decks_.erase(it);
				promote = true;
			} else {
				++it;
			}
		}

		if (promote && current_) {
			for (const DeckPtr &deck : decks_)
				deck->driver.store(deck == current_);

			if (!current_->driver.load())
				current_->driver.store(true);

			current_->drain_backlog.store(true);
		}
	}

	for (DeckPtr &deck : dead)
		destroy_deck(deck);
}

/* A deck that has not produced audio yet may report STOPPED simply because the
 * decoder has not opened the file, so a short grace period keeps a slow start
 * from looking like the end of a track. */
bool MusicSource::deck_finished(const Deck &deck)
{
	constexpr uint64_t START_GRACE_NS = 3000000000ULL;

	const obs_media_state state = obs_source_media_get_state(deck.media);

	if (state == OBS_MEDIA_STATE_ERROR)
		return true;

	if (state != OBS_MEDIA_STATE_ENDED && state != OBS_MEDIA_STATE_STOPPED)
		return false;

	return deck.produced.load() || os_gettime_ns() - deck.created_ns > START_GRACE_NS;
}

MusicSource::DeckPtr MusicSource::current_deck()
{
	std::lock_guard<std::mutex> lock(decks_mutex_);
	return current_;
}

/* -------------------------------------------------------------------------- */
/* Playback state                                                              */
/* -------------------------------------------------------------------------- */

void MusicSource::note_track_change(int entry_index)
{
	std::lock_guard<std::mutex> lock(state_mutex_);

	playlist_.set_current(entry_index);
	track_serial_++;
	last_media_ms_ = 0;
	last_media_ns_ = os_gettime_ns();
	duration_ms_ = 0;
	time_reset_ = true;
	crossfade_active_ = false;
	transport_ = PlayState::Playing;
}

void MusicSource::advance_track(int crossfade_ms, bool user_initiated)
{
	int target;
	RepeatMode repeat;
	int gap_ms;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		repeat = repeat_;
		gap_ms = gap_ms_;

		/* An explicit skip should move on rather than replay the track. */
		if (user_initiated && repeat == RepeatMode::One)
			repeat = RepeatMode::All;

		target = playlist_.advance(repeat);
	}

	if (target < 0) {
		stop();
		return;
	}

	if (crossfade_ms <= 0 && gap_ms > 0) {
		std::vector<DeckPtr> dead;

		{
			std::lock_guard<std::mutex> lock(decks_mutex_);
			dead.swap(decks_);
			current_.reset();
		}

		for (DeckPtr &deck : dead)
			destroy_deck(deck);

		std::lock_guard<std::mutex> lock(state_mutex_);
		pending_entry_ = target;
		pending_start_ns_ = os_gettime_ns() + (uint64_t)gap_ms * 1000000ULL;
		return;
	}

	start_entry(target, crossfade_ms);
}

void MusicSource::play()
{
	DeckPtr deck = current_deck();

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		pending_pause_ns_ = 0;
		pending_stop_ns_ = 0;

		if (deck && transport_ == PlayState::Paused) {
			transport_ = PlayState::Playing;

			audio_t *obs_audio = obs_get_audio();
			const size_t rate = obs_audio ? audio_output_get_sample_rate(obs_audio) : 48000;

			deck->set_gain(fade_in_ms_ > 0 ? 0.0 : 1.0);
			if (fade_in_ms_ > 0)
				deck->fade_to(1.0, ms_to_samples(fade_in_ms_, rate), false);

			obs_source_media_play_pause(deck->media, false);
			time_reset_ = true;
			return;
		}

		if (deck && transport_ == PlayState::Playing)
			return;
	}

	int target;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		target = playlist_.current();
		if (target < 0)
			target = playlist_.first();
	}

	if (target >= 0)
		start_entry(target, 0);
}

void MusicSource::pause()
{
	DeckPtr deck = current_deck();
	if (!deck)
		return;

	std::lock_guard<std::mutex> lock(state_mutex_);

	if (transport_ != PlayState::Playing)
		return;

	transport_ = PlayState::Paused;

	if (fade_out_ms_ > 0) {
		audio_t *obs_audio = obs_get_audio();
		const size_t rate = obs_audio ? audio_output_get_sample_rate(obs_audio) : 48000;

		deck->fade_to(0.0, ms_to_samples(fade_out_ms_, rate), false);
		pending_pause_ns_ = os_gettime_ns() + (uint64_t)fade_out_ms_ * 1000000ULL;
	} else {
		obs_source_media_play_pause(deck->media, true);
	}
}

void MusicSource::toggle_pause()
{
	PlayState state;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		state = transport_;
	}

	if (state == PlayState::Playing)
		pause();
	else
		play();
}

void MusicSource::stop()
{
	int fade_out_ms;
	DeckPtr deck = current_deck();

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		fade_out_ms = fade_out_ms_;
		pending_entry_ = -1;
		pending_pause_ns_ = 0;

		if (deck && fade_out_ms > 0 && transport_ == PlayState::Playing) {
			audio_t *obs_audio = obs_get_audio();
			const size_t rate = obs_audio ? audio_output_get_sample_rate(obs_audio) : 48000;

			deck->fade_to(0.0, ms_to_samples(fade_out_ms, rate), false);
			pending_stop_ns_ = os_gettime_ns() + (uint64_t)fade_out_ms * 1000000ULL;
			return;
		}

		transport_ = PlayState::Stopped;
		pending_stop_ns_ = 0;
		last_media_ms_ = 0;
		duration_ms_ = 0;
		time_reset_ = true;
		consecutive_errors_ = 0;
	}

	retire_all_decks();
}

void MusicSource::next()
{
	advance_track(manual_crossfade_ms(), true);
}

void MusicSource::previous()
{
	int target;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		target = playlist_.step_back();
	}

	if (target >= 0)
		start_entry(target, manual_crossfade_ms());
}

void MusicSource::restart()
{
	DeckPtr deck = current_deck();

	if (!deck) {
		play();
		return;
	}

	obs_source_media_restart(deck->media);

	std::lock_guard<std::mutex> lock(state_mutex_);
	transport_ = PlayState::Playing;
	last_media_ms_ = 0;
	last_media_ns_ = os_gettime_ns();
	time_reset_ = true;
}

void MusicSource::seek(int64_t ms)
{
	DeckPtr deck = current_deck();
	if (!deck)
		return;

	obs_source_media_set_time(deck->media, ms);

	std::lock_guard<std::mutex> lock(state_mutex_);
	last_media_ms_ = ms;
	last_media_ns_ = os_gettime_ns();
	time_reset_ = true;
}

void MusicSource::play_index(int index)
{
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (!playlist_.at(index))
			return;
	}

	start_entry(index, manual_crossfade_ms());
}

/* Crossfade length for a skip the user asked for, clamped so that a short
 * track cannot be swallowed whole by the fade. */
int MusicSource::manual_crossfade_ms()
{
	int crossfade_ms;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		crossfade_ms = crossfade_ms_;
	}

	if (crossfade_ms <= 0)
		return 0;

	const int64_t remaining = media_duration() - media_time();
	if (remaining > 0 && remaining < crossfade_ms)
		crossfade_ms = (int)remaining;

	return crossfade_ms;
}

void MusicSource::activate()
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	if (!keep_playing_when_hidden_ && paused_by_inactive_) {
		paused_by_inactive_ = false;
		wake_flag_ = true;
		resume_on_tick_ = true;
		cv_.notify_all();
	}
}

void MusicSource::deactivate()
{
	bool should_pause = false;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (!keep_playing_when_hidden_ && transport_ == PlayState::Playing) {
			paused_by_inactive_ = true;
			should_pause = true;
		}
	}

	if (should_pause)
		pause();
}

/* -------------------------------------------------------------------------- */
/* Housekeeping                                                                */
/* -------------------------------------------------------------------------- */

void MusicSource::wake()
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	wake_flag_ = true;
	cv_.notify_all();
}

void MusicSource::housekeeping_thread()
{
	os_set_thread_name("void-radio-music");

	while (!stop_thread_.load()) {
		{
			std::unique_lock<std::mutex> lock(state_mutex_);
			cv_.wait_for(lock, std::chrono::milliseconds(TICK_INTERVAL_MS),
				     [this]() { return wake_flag_ || stop_thread_.load(); });
			wake_flag_ = false;
		}

		if (stop_thread_.load())
			break;

		tick();
	}
}

void MusicSource::tick()
{
	bool rebuild;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		rebuild = rebuild_pending_;
	}

	if (rebuild)
		rebuild_playlist();

	load_pending_tags();

	if (resume_on_tick_.exchange(false))
		play();

	bool autostart = false;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (autostart_pending_ && !playlist_.empty()) {
			autostart_pending_ = false;
			autostart = true;
		}
	}

	if (autostart)
		play();

	/* Deferred fade-outs. */
	{
		DeckPtr deck = current_deck();
		bool do_stop = false;
		const uint64_t now = os_gettime_ns();

		{
			std::lock_guard<std::mutex> lock(state_mutex_);

			if (pending_pause_ns_ && now >= pending_pause_ns_) {
				pending_pause_ns_ = 0;
				if (deck)
					obs_source_media_play_pause(deck->media, true);
			}

			if (pending_stop_ns_ && now >= pending_stop_ns_) {
				pending_stop_ns_ = 0;
				do_stop = true;
			}
		}

		if (do_stop) {
			{
				std::lock_guard<std::mutex> lock(state_mutex_);
				transport_ = PlayState::Stopped;
				last_media_ms_ = 0;
				duration_ms_ = 0;
			}
			retire_all_decks();
		}
	}

	/* Gap between tracks. */
	{
		int pending = -1;
		const uint64_t now = os_gettime_ns();

		{
			std::lock_guard<std::mutex> lock(state_mutex_);
			if (pending_entry_ >= 0 && now >= pending_start_ns_) {
				pending = pending_entry_;
				pending_entry_ = -1;
			}
		}

		if (pending >= 0)
			start_entry(pending, 0);
	}

	reap_decks();
	poll_playback();
	publish_snapshot();
}

void MusicSource::poll_playback()
{
	DeckPtr deck = current_deck();
	if (!deck)
		return;

	PlayState transport;
	int crossfade_ms;
	bool skip_on_error;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		transport = transport_;
		crossfade_ms = crossfade_ms_;
		skip_on_error = skip_on_error_;
	}

	if (transport == PlayState::Stopped)
		return;

	const obs_media_state state = obs_source_media_get_state(deck->media);
	const int64_t duration = obs_source_media_get_duration(deck->media);
	const int64_t time = obs_source_media_get_time(deck->media);
	const uint64_t now = os_gettime_ns();

	{
		std::lock_guard<std::mutex> lock(state_mutex_);

		duration_ms_ = duration > 0 ? duration : 0;

		/* Media time only updates when a packet is decoded, and can jitter
		 * backwards slightly. Interpolation in snapshot() smooths it out,
		 * so only accept a value that moves forward or clearly seeks. */
		const int64_t delta = time - last_media_ms_;
		if (time_reset_ || delta >= 0 || delta < -1000) {
			last_media_ms_ = time < 0 ? 0 : time;
			last_media_ns_ = now;
			time_reset_ = false;
		}
	}

	/* A deck that never produced a sample counts as a failure; one that
	 * played clears the counter. Without that, a playlist of unplayable
	 * files would skip forever. */
	if (state == OBS_MEDIA_STATE_ERROR || deck_finished(*deck)) {
		const bool produced = deck->produced.load();
		bool give_up = false;

		{
			std::lock_guard<std::mutex> lock(state_mutex_);

			if (produced) {
				consecutive_errors_ = 0;
			} else {
				consecutive_errors_++;
				give_up = !skip_on_error || consecutive_errors_ > (int)playlist_.size();
			}
		}

		if (!produced)
			obs_log(LOG_WARNING, "could not play track, %s",
				give_up ? "stopping playback" : "skipping to the next one");

		if (give_up)
			stop();
		else
			advance_track(0, false);

		return;
	}

	if (deck->produced.load()) {
		std::lock_guard<std::mutex> lock(state_mutex_);
		consecutive_errors_ = 0;
	}

	if (transport != PlayState::Playing || crossfade_ms <= 0 || duration <= 0)
		return;

	bool already_crossfading;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		already_crossfading = crossfade_active_;
	}

	/* Never fade for longer than half the track, so short jingles still get
	 * to play rather than being crossfaded away as soon as they start. */
	const int64_t effective = std::min<int64_t>(crossfade_ms, duration / 2);

	if (!already_crossfading && effective > 0 && duration - time <= effective) {
		{
			std::lock_guard<std::mutex> lock(state_mutex_);
			crossfade_active_ = true;
		}

		advance_track((int)effective, false);
	}
}

void MusicSource::publish_snapshot()
{
	Snapshot snapshot;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);

		const PlaylistEntry *entry = playlist_.at(playlist_.current());

		snapshot.valid = true;
		snapshot.state = transport_;
		snapshot.count = (int)playlist_.size();
		snapshot.index = playlist_.current();
		snapshot.elapsed_ms = last_media_ms_;
		snapshot.duration_ms = duration_ms_;
		snapshot.track_serial = track_serial_;

		if (entry) {
			snapshot.path = entry->path;
			snapshot.title = entry->display_title();
			snapshot.artist = entry->display_artist();
			snapshot.album = entry->tags.album;
			snapshot.genre = entry->tags.genre;
			snapshot.year = entry->tags.year;
			snapshot.track = entry->tags.track;
			snapshot.seekable = !entry->url && duration_ms_ > 0;
		}

		const PlaylistEntry *next_entry = playlist_.at(playlist_.peek_next(repeat_));
		if (next_entry) {
			snapshot.next_title = next_entry->display_title();
			snapshot.next_artist = next_entry->display_artist();
		}
	}

	std::lock_guard<std::mutex> lock(snapshot_mutex_);
	cached_ = std::move(snapshot);
	cached_ns_ = os_gettime_ns();
}

bool MusicSource::snapshot(Snapshot &out)
{
	std::lock_guard<std::mutex> lock(snapshot_mutex_);

	out = cached_;

	if (!out.valid)
		return false;

	/* Extrapolate between decoder updates so the progress bar moves every
	 * frame instead of stepping once per audio packet. */
	if (out.state == PlayState::Playing && cached_ns_) {
		const uint64_t now = os_gettime_ns();
		if (now > cached_ns_)
			out.elapsed_ms += (int64_t)((now - cached_ns_) / 1000000ULL);
	}

	if (out.elapsed_ms < 0)
		out.elapsed_ms = 0;

	if (out.duration_ms > 0) {
		if (out.elapsed_ms > out.duration_ms)
			out.elapsed_ms = out.duration_ms;

		out.progress = (float)((double)out.elapsed_ms / (double)out.duration_ms);
	} else {
		out.progress = 0.0f;
	}

	return true;
}

void MusicSource::entries(std::vector<EntryInfo> &out, int &current_index)
{
	std::lock_guard<std::mutex> lock(state_mutex_);

	out.clear();
	out.reserve(playlist_.size());

	for (const PlaylistEntry &entry : playlist_.entries()) {
		EntryInfo info;
		info.path = entry.path;
		info.title = entry.display_title();
		info.artist = entry.display_artist();
		info.url = entry.url;
		info.missing = entry.missing;
		out.push_back(std::move(info));
	}

	current_index = playlist_.current();
}

int64_t MusicSource::media_time()
{
	Snapshot snap;
	return snapshot(snap) ? snap.elapsed_ms : 0;
}

int64_t MusicSource::media_duration()
{
	Snapshot snap;
	return snapshot(snap) ? snap.duration_ms : 0;
}

obs_media_state MusicSource::media_state()
{
	std::lock_guard<std::mutex> lock(state_mutex_);

	switch (transport_) {
	case PlayState::Playing:
		return OBS_MEDIA_STATE_PLAYING;
	case PlayState::Paused:
		return OBS_MEDIA_STATE_PAUSED;
	default:
		return playlist_.empty() ? OBS_MEDIA_STATE_NONE : OBS_MEDIA_STATE_STOPPED;
	}
}

/* -------------------------------------------------------------------------- */
/* Settings written from the dock                                              */
/* -------------------------------------------------------------------------- */

void MusicSource::set_shuffle(bool shuffle)
{
	write_setting_bool("shuffle", shuffle);
}

void MusicSource::set_repeat(RepeatMode mode)
{
	write_setting_int("repeat", (int64_t)mode);
}

void MusicSource::set_override(const std::string &path, const std::string &title, const std::string &artist)
{
	obs_data_t *data = settings();
	obs_data_array_t *array = obs_data_get_array(data, "overrides");

	if (!array) {
		array = obs_data_array_create();
		obs_data_set_array(data, "overrides", array);
	}

	bool replaced = false;
	const size_t count = obs_data_array_count(array);

	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(array, i);

		if (path == obs_data_get_string(item, "path")) {
			obs_data_set_string(item, "title", title.c_str());
			obs_data_set_string(item, "artist", artist.c_str());
			replaced = true;
		}

		obs_data_release(item);

		if (replaced)
			break;
	}

	if (!replaced && (!title.empty() || !artist.empty())) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "path", path.c_str());
		obs_data_set_string(item, "title", title.c_str());
		obs_data_set_string(item, "artist", artist.c_str());
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}

	obs_source_update(self_, data);
	obs_data_array_release(array);
	obs_data_release(data);

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		PlaylistEntry *entry = playlist_.at(playlist_.index_of(path));
		if (entry) {
			entry->override_title = title;
			entry->override_artist = artist;
		}
	}

	wake();
}

void MusicSource::set_order(const std::vector<std::string> &paths)
{
	obs_data_t *data = settings();
	obs_data_array_t *array = obs_data_array_create();

	for (const std::string &path : paths) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "value", path.c_str());
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}

	obs_data_set_array(data, "playlist", array);
	obs_source_update(self_, data);

	obs_data_array_release(array);
	obs_data_release(data);
}

void MusicSource::remove_path(const std::string &path)
{
	std::vector<EntryInfo> current;
	int index;
	entries(current, index);

	std::vector<std::string> paths;
	paths.reserve(current.size());

	for (const EntryInfo &info : current) {
		if (info.path != path)
			paths.push_back(info.path);
	}

	set_order(paths);
}

/* -------------------------------------------------------------------------- */
/* Hotkeys                                                                     */
/* -------------------------------------------------------------------------- */

struct MusicSource::HotkeyContext {
	MusicSource *source;
	int action;
};

void MusicSource::hotkey_pressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	if (!pressed)
		return;

	HotkeyContext *context = static_cast<HotkeyContext *>(data);
	if (!context || !context->source)
		return;

	switch (context->action) {
	case 0:
		context->source->toggle_pause();
		break;
	case 1:
		context->source->stop();
		break;
	case 2:
		context->source->next();
		break;
	case 3:
		context->source->previous();
		break;
	case 4:
		context->source->restart();
		break;
	default:
		break;
	}
}

void MusicSource::register_hotkeys()
{
	struct Definition {
		const char *name;
		const char *text;
		int action;
		obs_hotkey_id *id;
	};

	const Definition definitions[] = {
		{"VoidRadio.PlayPause", "Hotkey.PlayPause", 0, &hotkey_play_pause_},
		{"VoidRadio.Stop", "Hotkey.Stop", 1, &hotkey_stop_},
		{"VoidRadio.Next", "Hotkey.Next", 2, &hotkey_next_},
		{"VoidRadio.Previous", "Hotkey.Previous", 3, &hotkey_previous_},
		{"VoidRadio.Restart", "Hotkey.Restart", 4, &hotkey_restart_},
	};

	for (const Definition &definition : definitions) {
		HotkeyContext *context = new HotkeyContext{this, definition.action};
		hotkey_contexts_.push_back(context);

		*definition.id = obs_hotkey_register_source(self_, definition.name, obs_module_text(definition.text),
							    hotkey_pressed, context);
	}
}

void MusicSource::unregister_hotkeys()
{
	const obs_hotkey_id ids[] = {hotkey_play_pause_, hotkey_stop_, hotkey_next_, hotkey_previous_, hotkey_restart_};

	for (obs_hotkey_id id : ids) {
		if (id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(id);
	}

	for (HotkeyContext *context : hotkey_contexts_)
		delete context;

	hotkey_contexts_.clear();
}

/* -------------------------------------------------------------------------- */
/* OBS registration                                                            */
/* -------------------------------------------------------------------------- */

namespace {

const char *source_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MusicSource");
}

void *source_create(obs_data_t *settings, obs_source_t *source)
{
	return new MusicSource(settings, source);
}

void source_destroy(void *data)
{
	delete static_cast<MusicSource *>(data);
}

void source_update(void *data, obs_data_t *settings)
{
	static_cast<MusicSource *>(data)->update(settings);
}

void source_activate(void *data)
{
	static_cast<MusicSource *>(data)->activate();
}

void source_deactivate(void *data)
{
	static_cast<MusicSource *>(data)->deactivate();
}

void source_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "recurse_dirs", true);
	obs_data_set_default_bool(settings, "shuffle", false);
	obs_data_set_default_int(settings, "repeat", (int64_t)RepeatMode::All);
	obs_data_set_default_int(settings, "crossfade_ms", 0);
	obs_data_set_default_int(settings, "gap_ms", 0);
	obs_data_set_default_int(settings, "fade_in_ms", 0);
	obs_data_set_default_int(settings, "fade_out_ms", 0);
	obs_data_set_default_bool(settings, "play_on_start", true);
	obs_data_set_default_bool(settings, "keep_playing_when_hidden", true);
	obs_data_set_default_bool(settings, "skip_on_error", true);
}

obs_properties_t *source_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_editable_list(props, "playlist", obs_module_text("Music.Playlist"),
					 OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS,
					 obs_module_text("Music.Playlist.Filter"), nullptr);

	obs_properties_add_bool(props, "recurse_dirs", obs_module_text("Music.RecurseDirs"));
	obs_properties_add_bool(props, "shuffle", obs_module_text("Music.Shuffle"));

	obs_property_t *repeat = obs_properties_add_list(props, "repeat", obs_module_text("Music.Repeat"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(repeat, obs_module_text("Music.Repeat.Off"), (int64_t)RepeatMode::Off);
	obs_property_list_add_int(repeat, obs_module_text("Music.Repeat.All"), (int64_t)RepeatMode::All);
	obs_property_list_add_int(repeat, obs_module_text("Music.Repeat.One"), (int64_t)RepeatMode::One);

	obs_property_t *crossfade =
		obs_properties_add_int_slider(props, "crossfade_ms", obs_module_text("Music.Crossfade"), 0, 15000, 100);
	obs_property_int_set_suffix(crossfade, " ms");

	obs_property_t *gap =
		obs_properties_add_int_slider(props, "gap_ms", obs_module_text("Music.Gap"), 0, 10000, 100);
	obs_property_int_set_suffix(gap, " ms");
	obs_property_set_long_description(gap, obs_module_text("Music.Gap.Description"));

	obs_property_t *fade_in =
		obs_properties_add_int_slider(props, "fade_in_ms", obs_module_text("Music.FadeIn"), 0, 10000, 100);
	obs_property_int_set_suffix(fade_in, " ms");

	obs_property_t *fade_out =
		obs_properties_add_int_slider(props, "fade_out_ms", obs_module_text("Music.FadeOut"), 0, 10000, 100);
	obs_property_int_set_suffix(fade_out, " ms");

	obs_properties_add_bool(props, "play_on_start", obs_module_text("Music.PlayOnStart"));

	obs_property_t *keep_playing =
		obs_properties_add_bool(props, "keep_playing_when_hidden", obs_module_text("Music.KeepPlaying"));
	obs_property_set_long_description(keep_playing, obs_module_text("Music.KeepPlaying.Description"));

	obs_properties_add_bool(props, "skip_on_error", obs_module_text("Music.SkipOnError"));

	return props;
}

void source_media_play_pause(void *data, bool pause)
{
	MusicSource *music = static_cast<MusicSource *>(data);

	if (pause)
		music->pause();
	else
		music->play();
}

void source_media_restart(void *data)
{
	static_cast<MusicSource *>(data)->restart();
}

void source_media_stop(void *data)
{
	static_cast<MusicSource *>(data)->stop();
}

void source_media_next(void *data)
{
	static_cast<MusicSource *>(data)->next();
}

void source_media_previous(void *data)
{
	static_cast<MusicSource *>(data)->previous();
}

int64_t source_media_get_duration(void *data)
{
	return static_cast<MusicSource *>(data)->media_duration();
}

int64_t source_media_get_time(void *data)
{
	return static_cast<MusicSource *>(data)->media_time();
}

void source_media_set_time(void *data, int64_t ms)
{
	static_cast<MusicSource *>(data)->seek(ms);
}

enum obs_media_state source_media_get_state(void *data)
{
	return static_cast<MusicSource *>(data)->media_state();
}

struct obs_source_info music_source_info = {};

} // namespace

void MusicSource::register_type()
{
	music_source_info.id = MUSIC_SOURCE_ID;
	music_source_info.type = OBS_SOURCE_TYPE_INPUT;
	music_source_info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CONTROLLABLE_MEDIA;
	music_source_info.icon_type = OBS_ICON_TYPE_MEDIA;
	music_source_info.get_name = source_get_name;
	music_source_info.create = source_create;
	music_source_info.destroy = source_destroy;
	music_source_info.update = source_update;
	music_source_info.activate = source_activate;
	music_source_info.deactivate = source_deactivate;
	music_source_info.get_defaults = source_defaults;
	music_source_info.get_properties = source_properties;
	music_source_info.media_play_pause = source_media_play_pause;
	music_source_info.media_restart = source_media_restart;
	music_source_info.media_stop = source_media_stop;
	music_source_info.media_next = source_media_next;
	music_source_info.media_previous = source_media_previous;
	music_source_info.media_get_duration = source_media_get_duration;
	music_source_info.media_get_time = source_media_get_time;
	music_source_info.media_set_time = source_media_set_time;
	music_source_info.media_get_state = source_media_get_state;

	obs_register_source(&music_source_info);
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

bool is_music_source(obs_source_t *source)
{
	return MusicSource::from_source(source) != nullptr;
}

bool get_snapshot(obs_source_t *source, Snapshot &out)
{
	MusicSource *music = MusicSource::from_source(source);
	return music ? music->snapshot(out) : false;
}

bool get_entries(obs_source_t *source, std::vector<EntryInfo> &out, int &current_index)
{
	MusicSource *music = MusicSource::from_source(source);
	if (!music)
		return false;

	music->entries(out, current_index);
	return true;
}

void transport_play(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->play();
}

void transport_pause(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->pause();
}

void transport_toggle_pause(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->toggle_pause();
}

void transport_stop(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->stop();
}

void transport_next(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->next();
}

void transport_previous(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->previous();
}

void transport_restart(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->restart();
}

void transport_seek(obs_source_t *source, int64_t ms)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->seek(ms);
}

void transport_play_index(obs_source_t *source, int index)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->play_index(index);
}

void set_shuffle(obs_source_t *source, bool shuffle)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->set_shuffle(shuffle);
}

void set_repeat(obs_source_t *source, RepeatMode mode)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->set_repeat(mode);
}

void set_entry_override(obs_source_t *source, const std::string &path, const std::string &title,
			const std::string &artist)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->set_override(path, title, artist);
}

void set_entry_order(obs_source_t *source, const std::vector<std::string> &paths)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->set_order(paths);
}

void remove_entry(obs_source_t *source, const std::string &path)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->remove_path(path);
}

void enum_music_sources(std::vector<obs_source_t *> &out)
{
	out.clear();

	auto collect = [](void *param, obs_source_t *source) -> bool {
		auto *list = static_cast<std::vector<obs_source_t *> *>(param);

		if (is_music_source(source)) {
			obs_source_t *ref = obs_source_get_ref(source);
			if (ref)
				list->push_back(ref);
		}

		return true;
	};

	obs_enum_sources(collect, &out);
}

} // namespace vr
