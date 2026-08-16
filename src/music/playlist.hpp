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

#include "music/track-tags.hpp"

#include <random>
#include <string>
#include <vector>

namespace vr {

enum class RepeatMode {
	Off = 0, /* stop once the last entry finishes */
	All = 1, /* wrap around to the start */
	One = 2, /* repeat the current entry forever */
};

struct PlaylistEntry {
	std::string path;
	bool url = false;
	bool tags_loaded = false;
	bool missing = false;
	Tags tags;
	std::string override_title;
	std::string override_artist;

	/* Falls back from the manual override, to the embedded tag, to the
	 * file name so that a track always has something to display. */
	std::string display_title() const;
	std::string display_artist() const;
};

/* Expands directories into their audio files and drops unplayable entries. */
std::vector<std::string> expand_paths(const std::vector<std::string> &paths, bool recurse);

class Playlist {
public:
	Playlist();

	void set_entries(std::vector<PlaylistEntry> entries);
	const std::vector<PlaylistEntry> &entries() const { return entries_; }
	std::vector<PlaylistEntry> &entries() { return entries_; }

	size_t size() const { return entries_.size(); }
	bool empty() const { return entries_.empty(); }
	const PlaylistEntry *at(int index) const;
	PlaylistEntry *at(int index);
	int index_of(const std::string &path) const;

	void set_shuffle(bool shuffle);
	bool shuffle() const { return shuffle_; }
	void reshuffle();

	int current() const { return current_; }
	void set_current(int entry_index);

	/* Navigation returns an entry index, or -1 when playback should stop. */
	int advance(RepeatMode mode);
	int peek_next(RepeatMode mode) const;
	int step_back();
	int first() const;

private:
	void rebuild_order();
	int order_position() const;

	std::vector<PlaylistEntry> entries_;
	std::vector<int> order_;
	int current_ = -1;
	bool shuffle_ = false;
	std::mt19937 rng_;
};

} // namespace vr
