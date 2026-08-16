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

enum class TimeFormat {
	Auto = 0,   /* h:mm:ss only once the value passes an hour */
	MSS = 1,    /* m:ss */
	MMSS = 2,   /* mm:ss */
	HMMSS = 3,  /* h:mm:ss */
	HHMMSS = 4, /* hh:mm:ss */
};

/* Formats a millisecond count as a clock string. Negative values clamp to 0. */
std::string format_time(int64_t ms, TimeFormat fmt);

std::string to_lower(const std::string &str);
std::string trim(const std::string &str);
bool iequals(const std::string &a, const std::string &b);

/* True when the path looks like a URL rather than a file on disk. */
bool is_url(const std::string &path);

/* Filename without directories, and without the extension. */
std::string file_stem(const std::string &path);
std::string file_name(const std::string &path);
std::string file_extension(const std::string &path);

/* True when the extension is one the media source can decode as audio. */
bool is_supported_audio_file(const std::string &path);

/* Latin-1 and UTF-16 to UTF-8 conversion, used by the tag readers. */
std::string latin1_to_utf8(const uint8_t *data, size_t size);
std::string utf16_to_utf8(const uint8_t *data, size_t size, bool big_endian);

} // namespace vr
