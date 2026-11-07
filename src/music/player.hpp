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

#include "audio/audio-format.hpp"
#include "audio/deck.hpp"
#include "music/gain-envelope.hpp"
#include "music/playlist.hpp"
#include "music/snapshot.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vr {

struct PlayerSettings {
	RepeatMode repeat = RepeatMode::All;
	bool shuffle = false;
	int crossfade_ms = 0;
	int gap_ms = 0;
	int fade_in_ms = 0;
	int fade_out_ms = 0;
	bool skip_on_error = true;
};

/*
 * The playback engine's logic: playlist navigation, transport, fades,
 * crossfades, gaps and mixing, all driven by one clock - the number of samples
 * rendered.
 *
 * It has no threads, no locks and no OBS. Whoever owns it calls its methods
 * from a single thread and calls render() once per block of audio; everything
 * that happens, from a fade finishing to a track ending, happens inside that
 * call, at an exact sample. Decks come from a factory, so tests can drive the
 * whole thing with fake ones.
 */
class Player {
public:
	using DeckFactory = std::function<std::unique_ptr<Deck>(const PlaylistEntry &entry)>;
	using LogFn = std::function<void(const std::string &)>;

	Player(const AudioFormat &format, DeckFactory factory, LogFn log = {});
	~Player();

	Player(const Player &) = delete;
	Player &operator=(const Player &) = delete;

	/* Playlist and settings. */
	void set_settings(const PlayerSettings &settings);
	void set_entries(std::vector<PlaylistEntry> entries);
	void set_tags(const std::string &path, const Tags &tags);

	/* Transport. */
	void play();
	void pause();
	void toggle_pause();
	void stop();
	void next();
	void previous();
	void restart();
	void seek(int64_t ms);
	void play_index(int index);

	/*
	 * Renders `frames` samples into the planar buffers. Returns false when
	 * there is nothing to output at all (stopped, or paused once any fade
	 * out has finished); the buffers are silent then and should not be
	 * sent anywhere. While a deck is still opening, or a gap is running,
	 * this returns true with silence, which keeps the output continuous.
	 */
	bool render(float *const *out, size_t frames);

	/* State, for publishing. */
	PlayState transport() const { return transport_; }
	const Playlist &playlist() const { return playlist_; }
	const PlayerSettings &settings() const { return settings_; }

	/* Bumped whenever an entry, its tags, or the current entry changes. */
	uint64_t entries_version() const { return entries_version_; }

	/* Position and duration are read from the current deck; the snapshot
	 * is not interpolated. */
	void fill_snapshot(Snapshot &out) const;

	/* The next local file whose tags have not been read, preferring the
	 * current entry and the one after it. Empty when there is none. */
	std::string next_untagged() const;

private:
	struct Voice {
		std::unique_ptr<Deck> deck;
		std::string path;
		GainEnvelope gain;
	};

	enum class Pending {
		None,
		Pause, /* fading out, then paused */
		Stop,  /* fading out, then stopped */
	};

	std::unique_ptr<Voice> make_voice(int entry_index);
	std::unique_ptr<Voice> take_prefetched(int entry_index);

	void start_entry(int entry_index, int crossfade_ms);
	void advance(int crossfade_ms, bool user_initiated);
	void handle_finished();
	void stop_now();
	void note_track_change(int entry_index);
	int manual_crossfade_ms() const;

	void maybe_prefetch();
	void maybe_crossfade();

	/* Reads up to `frames` from `voice`, applies its gain and adds it into
	 * `out` starting at `offset`. Returns how many samples it produced. */
	size_t mix_voice(Voice &voice, float *const *out, size_t offset, size_t frames);
	static bool finished(const Voice &voice);

	uint64_t ms_frames(int64_t ms) const { return ms_to_frames(ms, format_.sample_rate); }

	const AudioFormat format_;
	DeckFactory factory_;
	LogFn log_;

	Playlist playlist_;
	PlayerSettings settings_;
	uint64_t entries_version_ = 0;
	uint64_t track_serial_ = 0;

	PlayState transport_ = PlayState::Stopped;
	Pending pending_ = Pending::None;

	std::unique_ptr<Voice> current_;
	std::vector<std::unique_ptr<Voice>> outgoing_;
	std::unique_ptr<Voice> prefetch_;
	bool crossfade_started_ = false;

	int gap_entry_ = -1;
	uint64_t gap_remaining_ = 0;

	int consecutive_errors_ = 0;

	std::vector<float> scratch_[MAX_MIX_CHANNELS];
};

} // namespace vr
