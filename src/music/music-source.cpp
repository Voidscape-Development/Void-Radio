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
#include "audio/stream-deck.hpp"

#include <plugin-support.h>
#include <media-io/audio-io.h>
#include <util/platform.h>
#include <util/threading.h>

extern "C" {
#include <libavutil/channel_layout.h>
}

#include <cstring>

namespace vr {

namespace {

/* One block of audio per engine wake-up. Short enough that a command lands
 * within a block of being posted, long enough to keep wake-ups cheap. */
constexpr uint64_t BLOCK_MS = 10;

/* If the engine thread falls this far behind its own timeline (the machine
 * stalled, or a debugger paused it), the timeline restarts from now rather than
 * sending a burst of audio stamped in the past. */
constexpr uint64_t MAX_LAG_NS = 200000000ULL;

/* How OBS's speaker layouts map onto FFmpeg channel orders, as libobs itself
 * maps them when it resamples. */
uint64_t channel_mask_for(enum speaker_layout speakers)
{
	switch (speakers) {
	case SPEAKERS_MONO:
		return AV_CH_LAYOUT_MONO;
	case SPEAKERS_STEREO:
		return AV_CH_LAYOUT_STEREO;
	case SPEAKERS_2POINT1:
		return AV_CH_LAYOUT_2POINT1;
	case SPEAKERS_4POINT0:
		return AV_CH_LAYOUT_4POINT0;
	case SPEAKERS_4POINT1:
		return AV_CH_LAYOUT_4POINT1;
	case SPEAKERS_5POINT1:
		return AV_CH_LAYOUT_5POINT1_BACK;
	case SPEAKERS_7POINT1:
		return AV_CH_LAYOUT_7POINT1;
	default:
		return 0;
	}
}

void log_warning(const std::string &message)
{
	obs_log(LOG_WARNING, "%s", message.c_str());
}

} // namespace

MusicSource *MusicSource::from_source(obs_source_t *source)
{
	if (!source)
		return nullptr;

	const char *id = obs_source_get_unversioned_id(source);
	if (!id || strcmp(id, MUSIC_SOURCE_ID) != 0)
		return nullptr;

	return static_cast<MusicSource *>(obs_obj_get_data(source));
}

MusicSource::MusicSource(obs_data_t *settings, obs_source_t *source) : self_(source)
{
	if (audio_t *audio = obs_get_audio()) {
		const struct audio_output_info *info = audio_output_get_info(audio);
		speakers_ = info->speakers;
		format_.sample_rate = info->samples_per_sec;
		format_.channels = (uint32_t)audio_output_get_channels(audio);
		format_.channel_mask = channel_mask_for(info->speakers);
	}

	if (format_.channels == 0 || format_.channels > MAX_MIX_CHANNELS)
		format_.channels = 2;

	const AudioFormat format = format_;
	player_ = std::make_unique<Player>(
		format,
		[format](const PlaylistEntry &entry) -> std::unique_ptr<Deck> {
			return std::make_unique<StreamDeck>(entry.path, entry.url, format, log_warning);
		},
		log_warning);

	library_ = std::make_unique<LibraryWorker>(
		[this](std::vector<PlaylistEntry> entries) {
			auto shared = std::make_shared<std::vector<PlaylistEntry>>(std::move(entries));
			post([this, shared]() {
				player_->set_entries(std::move(*shared));

				if (autostart_pending_ && !player_->playlist().empty()) {
					autostart_pending_ = false;
					player_->play();
				}
			});
		},
		[this](const std::string &path, const Tags &tags) {
			post([this, path, tags]() {
				player_->set_tags(path, tags);
				/* Tags handed over before measuring are followed by
				 * the measured ones; the read is not done yet. */
				if (tags_in_flight_ == path && !tags.measuring)
					tags_in_flight_.clear();
			});
		});

	update(settings);

	autostart_pending_ = obs_data_get_bool(settings, "play_on_start");

	hotkeys_ = std::make_unique<SourceHotkeys>(this);

	running_.store(true);
	thread_ = std::thread([this]() { engine_thread(); });
}

MusicSource::~MusicSource()
{
	hotkeys_.reset();

	/* Before anything else goes, so OBS's audio thread cannot call in. */
	unbind_duck_trigger();

	/* The library goes first so that it cannot post into a queue nobody
	 * will run; then the engine, which takes every deck with it. */
	library_->shutdown();

	running_.store(false);
	if (thread_.joinable())
		thread_.join();

	player_.reset();
	library_.reset();
}

/* -------------------------------------------------------------------------- */
/* Engine thread                                                               */
/* -------------------------------------------------------------------------- */

void MusicSource::engine_thread()
{
	os_set_thread_name("void-radio-engine");

	const size_t frames = (size_t)ms_to_frames((int64_t)BLOCK_MS, format_.sample_rate);
	const uint64_t block_ns = BLOCK_MS * 1000000ULL;

	for (uint32_t ch = 0; ch < format_.channels; ch++)
		block_[ch].assign(frames, 0.0f);

	uint64_t next_wake = os_gettime_ns();

	while (running_.load()) {
		commands_.run_pending();

		output_block(frames);
		publish();
		request_tags();

		/* While audio is going out, wake when the next block is due on
		 * its own timeline; otherwise just poll for commands. */
		if (timeline_running_)
			next_wake = timeline_start_ns_ + timeline_frames_ * 1000000000ULL / format_.sample_rate;
		else
			next_wake += block_ns;

		const uint64_t now = os_gettime_ns();
		if (next_wake + block_ns < now)
			next_wake = now;

		os_sleepto_ns(next_wake);
	}
}

void MusicSource::output_block(size_t frames)
{
	float *planes[MAX_MIX_CHANNELS] = {};
	for (uint32_t ch = 0; ch < format_.channels; ch++)
		planes[ch] = block_[ch].data();

	if (!player_->render(planes, frames)) {
		apply_duck(nullptr, frames);
		timeline_running_ = false;
		return;
	}

	apply_duck(planes, frames);

	const uint64_t now = os_gettime_ns();

	if (!timeline_running_) {
		timeline_running_ = true;
		timeline_start_ns_ = now;
		timeline_frames_ = 0;
	}

	uint64_t timestamp = timeline_start_ns_ + timeline_frames_ * 1000000000ULL / format_.sample_rate;

	if (now > timestamp + MAX_LAG_NS) {
		timeline_start_ns_ = now;
		timeline_frames_ = 0;
		timestamp = now;
	}

	struct obs_source_audio audio = {};
	for (uint32_t ch = 0; ch < format_.channels; ch++)
		audio.data[ch] = reinterpret_cast<const uint8_t *>(planes[ch]);

	audio.frames = (uint32_t)frames;
	audio.speakers = speakers_;
	audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	audio.samples_per_sec = format_.sample_rate;
	audio.timestamp = timestamp;

	obs_source_output_audio(self_, &audio);

	timeline_frames_ += frames;
}

void MusicSource::publish()
{
	Snapshot snapshot;
	player_->fill_snapshot(snapshot);

	{
		std::lock_guard<std::mutex> lock(snapshot_mutex_);
		snapshot_ = std::move(snapshot);
		snapshot_ns_ = os_gettime_ns();
	}

	if (player_->entries_version() == published_entries_version_)
		return;

	published_entries_version_ = player_->entries_version();

	std::vector<EntryInfo> entries;
	entries.reserve(player_->playlist().size());

	for (const PlaylistEntry &entry : player_->playlist().entries()) {
		EntryInfo info;
		info.path = entry.path;
		info.title = entry.display_title();
		info.artist = entry.display_artist();
		info.url = entry.url;
		info.missing = entry.missing;
		entries.push_back(std::move(info));
	}

	std::lock_guard<std::mutex> lock(entries_mutex_);
	entries_ = std::move(entries);
	current_index_ = player_->playlist().current();
}

void MusicSource::request_tags()
{
	if (!tags_in_flight_.empty())
		return;

	tags_in_flight_ = player_->next_untagged();
	if (!tags_in_flight_.empty())
		library_->read_tags_for(tags_in_flight_, player_->wants_measure());
}

/* -------------------------------------------------------------------------- */
/* Transport                                                                   */
/* -------------------------------------------------------------------------- */

void MusicSource::play()
{
	post([this]() { player_->play(); });
}

void MusicSource::pause()
{
	post([this]() { player_->pause(); });
}

void MusicSource::toggle_pause()
{
	post([this]() { player_->toggle_pause(); });
}

void MusicSource::stop()
{
	post([this]() { player_->stop(); });
}

void MusicSource::next()
{
	post([this]() { player_->next(); });
}

void MusicSource::previous()
{
	post([this]() { player_->previous(); });
}

void MusicSource::restart()
{
	post([this]() { player_->restart(); });
}

void MusicSource::seek(int64_t ms)
{
	post([this, ms]() { player_->seek(ms); });

	/* Show the new position straight away rather than a block later, so a
	 * scrubber that reads it back does not jump. */
	std::lock_guard<std::mutex> lock(snapshot_mutex_);
	if (snapshot_.seekable) {
		snapshot_.elapsed_ms = ms;
		snapshot_ns_ = os_gettime_ns();
	}
}

void MusicSource::play_index(int index)
{
	post([this, index]() { player_->play_index(index); });
}

/* Hidden sources pause unless they are told to keep playing, and pick up
 * again when shown - but only if hiding them is what paused them. */
void MusicSource::activate()
{
	post([this]() {
		if (!keep_playing_when_hidden_ && paused_by_inactive_) {
			paused_by_inactive_ = false;
			player_->play();
		}
	});
}

void MusicSource::deactivate()
{
	post([this]() {
		if (!keep_playing_when_hidden_ && player_->transport() == PlayState::Playing) {
			paused_by_inactive_ = true;
			player_->pause();
		}
	});
}

/* -------------------------------------------------------------------------- */
/* Published state                                                             */
/* -------------------------------------------------------------------------- */

bool MusicSource::snapshot(Snapshot &out) const
{
	uint64_t published_ns;

	{
		std::lock_guard<std::mutex> lock(snapshot_mutex_);
		out = snapshot_;
		published_ns = snapshot_ns_;
	}

	if (!out.valid)
		return false;

	/* Carry the position on between publishes so that anything drawn every
	 * frame moves smoothly. */
	if (out.state == PlayState::Playing && published_ns) {
		const uint64_t now = os_gettime_ns();
		if (now > published_ns)
			out.elapsed_ms += (int64_t)((now - published_ns) / 1000000ULL);
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

void MusicSource::entries(std::vector<EntryInfo> &out, int &current_index) const
{
	std::lock_guard<std::mutex> lock(entries_mutex_);
	out = entries_;
	current_index = current_index_;
}

int64_t MusicSource::media_time() const
{
	Snapshot snap;
	return snapshot(snap) ? snap.elapsed_ms : 0;
}

int64_t MusicSource::media_duration() const
{
	Snapshot snap;
	return snapshot(snap) ? snap.duration_ms : 0;
}

obs_media_state MusicSource::media_state() const
{
	Snapshot snap;
	if (!snapshot(snap))
		return OBS_MEDIA_STATE_NONE;

	switch (snap.state) {
	case PlayState::Playing:
		return OBS_MEDIA_STATE_PLAYING;
	case PlayState::Paused:
		return OBS_MEDIA_STATE_PAUSED;
	default:
		return snap.count ? OBS_MEDIA_STATE_STOPPED : OBS_MEDIA_STATE_NONE;
	}
}

} // namespace vr
