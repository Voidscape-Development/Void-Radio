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
#include <vector>

namespace vr {

/*
 * Cover art embedded in a media file.
 *
 * The bytes are whatever the tag held, still in their original container
 * (usually JPEG or PNG); decoding them is the caller's problem. `mime` is
 * advisory and may be empty even when bytes were found.
 */
struct AlbumArtData {
	std::vector<uint8_t> bytes;
	std::string mime;

	bool empty() const { return bytes.empty(); }
};

/*
 * Reads embedded cover art with FFmpeg: ID3v2 pictures in MP3, FLAC PICTURE
 * blocks, METADATA_BLOCK_PICTURE in Ogg Vorbis and Opus, and the MP4 "covr"
 * atom. WAV has no standard picture chunk.
 *
 * A front cover is preferred when a file carries several pictures; otherwise
 * the first one wins. Returns false when nothing was found, and never throws.
 */
bool read_embedded_art(const std::string &path, AlbumArtData &out);

/*
 * Looks for artwork sitting next to a track: cover, folder, front, album,
 * albumart, thumb or artwork with an image extension, matched without regard
 * to case. Returns an empty string when the directory holds none.
 */
std::string find_sidecar_art(const std::string &path);

} // namespace vr
