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

#include <string>

namespace vr {

struct Tags {
	std::string title;
	std::string artist;
	std::string album;
	std::string genre;
	std::string year;
	std::string track;

	bool empty() const
	{
		return title.empty() && artist.empty() && album.empty() && genre.empty() && year.empty() &&
		       track.empty();
	}
};

/*
 * Reads embedded metadata from a media file without pulling in a tagging
 * library. Supported containers: ID3v2 (2.2/2.3/2.4) and ID3v1 as used by MP3,
 * Vorbis comments in FLAC and Ogg (Vorbis/Opus/FLAC), iTunes-style metadata in
 * MP4/M4A, and RIFF INFO chunks in WAV.
 *
 * Returns true when at least one field could be filled in. Never throws, and
 * always leaves `out` in a usable state.
 */
bool read_tags(const std::string &path, Tags &out);

} // namespace vr
