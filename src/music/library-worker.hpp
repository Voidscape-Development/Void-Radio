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

#include "music/playlist.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vr {

/* What the user configured, before folders are expanded. */
struct PlaylistConfig {
	std::vector<std::string> paths;
	bool recurse = true;
	std::vector<PlaylistEntry> overrides; /* path, override_title, override_artist */

	bool operator==(const PlaylistConfig &other) const;
	bool operator!=(const PlaylistConfig &other) const { return !(*this == other); }
};

/*
 * The disk work behind a playlist, kept off the engine thread: walking folders
 * into a list of entries, and reading each track's tags. Results come back
 * through callbacks on the worker's thread, which the music source turns into
 * commands for the engine.
 */
class LibraryWorker {
public:
	using EntriesFn = std::function<void(std::vector<PlaylistEntry>)>;
	using TagsFn = std::function<void(const std::string &path, const Tags &tags)>;

	LibraryWorker(EntriesFn on_entries, TagsFn on_tags);
	~LibraryWorker();

	LibraryWorker(const LibraryWorker &) = delete;
	LibraryWorker &operator=(const LibraryWorker &) = delete;

	/* Replaces any rebuild that has not started yet. */
	void rebuild(PlaylistConfig config);

	/* Queues one tag read; the newest request wins. With `measure`, a file
	 * whose tags give no loudness is measured too: its tags are handed
	 * over first, marked as measuring, then again once it is measured. */
	void read_tags_for(const std::string &path, bool measure = false);

	/* Stops the thread. Callbacks never run after this returns. */
	void shutdown();

private:
	void run();

	EntriesFn on_entries_;
	TagsFn on_tags_;

	std::mutex mutex_;
	std::condition_variable cv_;
	bool stopping_ = false;

	bool rebuild_pending_ = false;
	PlaylistConfig config_;
	std::string tags_path_;
	bool tags_measure_ = false;
	std::atomic<bool> abort_{false};

	std::thread thread_;
};

} // namespace vr
