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

#include <cstdint>
#include <string>

namespace vr {

enum class PlayState {
	Stopped = 0,
	Playing = 1,
	Paused = 2,
};

/* Everything the progress bar, the info filter and the dock need to render one
 * frame, captured atomically so the fields can never disagree with each other. */
struct Snapshot {
	bool valid = false;
	PlayState state = PlayState::Stopped;

	std::string path;
	std::string title;
	std::string artist;
	std::string album;
	std::string genre;
	std::string year;
	std::string track;

	std::string next_title;
	std::string next_artist;

	int index = -1; /* zero based position of the current entry */
	int count = 0;

	int64_t elapsed_ms = 0;
	int64_t duration_ms = 0; /* 0 when unknown, as for a live stream */
	bool seekable = false;

	/* 0..1, already interpolated between decoder updates so the bar moves
	 * smoothly instead of stepping once per audio packet. */
	float progress = 0.0f;

	/* Bumped every time playback moves to a different entry. Consumers use
	 * it to detect a track change without comparing strings. */
	uint64_t track_serial = 0;
};

} // namespace vr
