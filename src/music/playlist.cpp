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

#include "music/playlist.hpp"
#include "util/vr-util.hpp"

#include <util/platform.h>

#include <algorithm>
#include <chrono>

namespace vr {

std::string PlaylistEntry::display_title() const
{
	if (!override_title.empty())
		return override_title;
	if (!tags.title.empty())
		return tags.title;

	return url ? path : file_stem(path);
}

std::string PlaylistEntry::display_artist() const
{
	if (!override_artist.empty())
		return override_artist;

	return tags.artist;
}

/* Directory walk, sorted so a folder plays in the order a file browser shows
 * it. Symlink loops are not followed because os_readdir does not report them,
 * so recursion is additionally depth limited. */
static void scan_directory(const std::string &dir, bool recurse, int depth, std::vector<std::string> &out)
{
	constexpr int MAX_DEPTH = 12;

	os_dir_t *handle = os_opendir(dir.c_str());
	if (!handle)
		return;

	std::vector<std::string> files;
	std::vector<std::string> subdirs;

	struct os_dirent *entry = os_readdir(handle);
	while (entry) {
		const std::string name = entry->d_name;

		if (name != "." && name != "..") {
			std::string full = dir;
			if (!full.empty() && full.back() != '/' && full.back() != '\\')
				full += '/';
			full += name;

			if (entry->directory) {
				if (recurse && depth < MAX_DEPTH)
					subdirs.push_back(full);
			} else if (is_supported_audio_file(name)) {
				files.push_back(full);
			}
		}

		entry = os_readdir(handle);
	}

	os_closedir(handle);

	std::sort(files.begin(), files.end());
	std::sort(subdirs.begin(), subdirs.end());

	out.insert(out.end(), files.begin(), files.end());

	for (const std::string &sub : subdirs)
		scan_directory(sub, recurse, depth + 1, out);
}

std::vector<std::string> expand_paths(const std::vector<std::string> &paths, bool recurse)
{
	std::vector<std::string> out;
	out.reserve(paths.size());

	for (const std::string &path : paths) {
		if (path.empty())
			continue;

		if (is_url(path)) {
			out.push_back(path);
			continue;
		}

		/* A path is only treated as a folder when it opens as one. */
		os_dir_t *dir = os_opendir(path.c_str());
		if (dir) {
			os_closedir(dir);
			scan_directory(path, recurse, 0, out);
			continue;
		}

		out.push_back(path);
	}

	return out;
}

Playlist::Playlist() : rng_((unsigned)std::chrono::steady_clock::now().time_since_epoch().count()) {}

const PlaylistEntry *Playlist::at(int index) const
{
	if (index < 0 || (size_t)index >= entries_.size())
		return nullptr;

	return &entries_[(size_t)index];
}

PlaylistEntry *Playlist::at(int index)
{
	if (index < 0 || (size_t)index >= entries_.size())
		return nullptr;

	return &entries_[(size_t)index];
}

int Playlist::index_of(const std::string &path) const
{
	for (size_t i = 0; i < entries_.size(); i++) {
		if (entries_[i].path == path)
			return (int)i;
	}

	return -1;
}

void Playlist::set_entries(std::vector<PlaylistEntry> entries)
{
	const std::string current_path =
		current_ >= 0 && (size_t)current_ < entries_.size() ? entries_[(size_t)current_].path : std::string();

	entries_ = std::move(entries);
	current_ = current_path.empty() ? -1 : index_of(current_path);

	rebuild_order();
}

void Playlist::set_shuffle(bool shuffle)
{
	if (shuffle_ == shuffle)
		return;

	shuffle_ = shuffle;
	rebuild_order();
}

void Playlist::reshuffle()
{
	if (shuffle_)
		rebuild_order();
}

void Playlist::set_current(int entry_index)
{
	if (entry_index < 0 || (size_t)entry_index >= entries_.size()) {
		current_ = -1;
		return;
	}

	current_ = entry_index;
}

/* The play order is a permutation of entry indices. In shuffle mode the
 * currently playing entry is pinned to the front so that "next" never replays
 * it immediately after a reshuffle. */
void Playlist::rebuild_order()
{
	order_.resize(entries_.size());
	for (size_t i = 0; i < entries_.size(); i++)
		order_[i] = (int)i;

	if (!shuffle_ || order_.size() < 2)
		return;

	std::shuffle(order_.begin(), order_.end(), rng_);

	if (current_ >= 0) {
		const auto it = std::find(order_.begin(), order_.end(), current_);
		if (it != order_.end())
			std::iter_swap(order_.begin(), it);
	}
}

int Playlist::order_position() const
{
	if (current_ < 0)
		return -1;

	const auto it = std::find(order_.begin(), order_.end(), current_);
	return it == order_.end() ? -1 : (int)(it - order_.begin());
}

int Playlist::first() const
{
	if (order_.empty())
		return -1;

	return order_.front();
}

int Playlist::peek_next(RepeatMode mode) const
{
	if (order_.empty())
		return -1;

	if (mode == RepeatMode::One)
		return current_;

	const int pos = order_position();
	if (pos < 0)
		return order_.front();

	if ((size_t)(pos + 1) < order_.size())
		return order_[(size_t)pos + 1];

	return mode == RepeatMode::All ? order_.front() : -1;
}

int Playlist::advance(RepeatMode mode)
{
	if (order_.empty())
		return -1;

	if (mode == RepeatMode::One && current_ >= 0)
		return current_;

	const int pos = order_position();

	if (pos < 0) {
		current_ = order_.front();
		return current_;
	}

	if ((size_t)(pos + 1) < order_.size()) {
		current_ = order_[(size_t)pos + 1];
		return current_;
	}

	if (mode != RepeatMode::All)
		return -1;

	/* Wrapping around is a good moment to pick a fresh shuffle order. */
	if (shuffle_) {
		const int previous = current_;
		current_ = -1;
		rebuild_order();
		current_ = previous;

		if (order_.size() > 1 && order_.front() == previous)
			std::iter_swap(order_.begin(), order_.end() - 1);
	}

	current_ = order_.front();
	return current_;
}

int Playlist::step_back()
{
	if (order_.empty())
		return -1;

	const int pos = order_position();

	if (pos <= 0) {
		current_ = order_.back();
		return current_;
	}

	current_ = order_[(size_t)pos - 1];
	return current_;
}

} // namespace vr
