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

#include "music/library-worker.hpp"
#include "music/loudness-scan.hpp"
#include "music/track-tags.hpp"
#include "util/vr-util.hpp"

#include <util/platform.h>
#include <util/threading.h>

#include <cmath>

namespace vr {

bool PlaylistConfig::operator==(const PlaylistConfig &other) const
{
	if (paths != other.paths || recurse != other.recurse || overrides.size() != other.overrides.size())
		return false;

	for (size_t i = 0; i < overrides.size(); i++) {
		const PlaylistEntry &a = overrides[i];
		const PlaylistEntry &b = other.overrides[i];
		if (a.path != b.path || a.override_title != b.override_title || a.override_artist != b.override_artist)
			return false;
	}

	return true;
}

namespace {

std::vector<PlaylistEntry> build_entries(const PlaylistConfig &config)
{
	const std::vector<std::string> expanded = expand_paths(config.paths, config.recurse);

	std::vector<PlaylistEntry> entries;
	entries.reserve(expanded.size());

	for (const std::string &path : expanded) {
		PlaylistEntry entry;
		entry.path = path;
		entry.url = is_url(path);
		entry.missing = !entry.url && !os_file_exists(path.c_str());

		for (const PlaylistEntry &override_entry : config.overrides) {
			if (override_entry.path == path) {
				entry.override_title = override_entry.override_title;
				entry.override_artist = override_entry.override_artist;
				break;
			}
		}

		entries.push_back(std::move(entry));
	}

	return entries;
}

} // namespace

LibraryWorker::LibraryWorker(EntriesFn on_entries, TagsFn on_tags)
	: on_entries_(std::move(on_entries)),
	  on_tags_(std::move(on_tags))
{
	thread_ = std::thread([this]() { run(); });
}

LibraryWorker::~LibraryWorker()
{
	shutdown();
}

void LibraryWorker::shutdown()
{
	abort_.store(true);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = true;
	}
	cv_.notify_all();

	if (thread_.joinable())
		thread_.join();
}

void LibraryWorker::rebuild(PlaylistConfig config)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		config_ = std::move(config);
		rebuild_pending_ = true;
	}
	cv_.notify_all();
}

void LibraryWorker::read_tags_for(const std::string &path, bool measure)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		tags_path_ = path;
		tags_measure_ = measure;
	}
	cv_.notify_all();
}

void LibraryWorker::run()
{
	os_set_thread_name("void-radio-library");

	for (;;) {
		bool rebuild = false;
		PlaylistConfig config;
		std::string tags_path;
		bool measure = false;

		{
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait(lock, [this]() { return stopping_ || rebuild_pending_ || !tags_path_.empty(); });

			if (stopping_)
				return;

			/* A rebuild goes first: tags for a list about to be replaced
			 * are not worth reading. */
			if (rebuild_pending_) {
				rebuild = true;
				rebuild_pending_ = false;
				config = config_;
			} else {
				tags_path.swap(tags_path_);
				measure = tags_measure_;
			}
		}

		if (rebuild) {
			on_entries_(build_entries(config));
			continue;
		}

		Tags tags;
		read_tags(tags_path, tags);

		if (!measure || is_url(tags_path) || !std::isnan(tags.track_loudness)) {
			tags.loudness_checked = measure;
			on_tags_(tags_path, tags);
			continue;
		}

		float loudness = 0.0f;
		float peak = 0.0f;

		if (cached_loudness(tags_path, loudness, peak)) {
			tags.track_loudness = loudness;
			tags.track_peak = peak;
			tags.loudness_checked = true;
			on_tags_(tags_path, tags);
			continue;
		}

		/* The names go up straight away; measuring takes a moment. */
		Tags early = tags;
		early.measuring = true;
		on_tags_(tags_path, early);

		const InterruptFn interrupt = [this]() {
			return abort_.load();
		};

		if (measure_loudness(tags_path, loudness, peak, &interrupt)) {
			store_loudness(tags_path, loudness, peak);
			tags.track_loudness = loudness;
			tags.track_peak = peak;
		}

		if (abort_.load())
			return;

		tags.loudness_checked = true;
		on_tags_(tags_path, tags);
	}
}

} // namespace vr
