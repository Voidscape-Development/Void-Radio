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

#include "util/vr-util.hpp"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace vr {

std::string format_time(int64_t ms, TimeFormat fmt)
{
	if (ms < 0)
		ms = 0;

	const int64_t total_seconds = ms / 1000;
	const int64_t hours = total_seconds / 3600;
	const int64_t minutes = (total_seconds / 60) % 60;
	const int64_t seconds = total_seconds % 60;

	char buf[32];

	switch (fmt) {
	case TimeFormat::MSS:
		snprintf(buf, sizeof(buf), "%" PRId64 ":%02" PRId64, total_seconds / 60, seconds);
		break;
	case TimeFormat::MMSS:
		snprintf(buf, sizeof(buf), "%02" PRId64 ":%02" PRId64, total_seconds / 60, seconds);
		break;
	case TimeFormat::HMMSS:
		snprintf(buf, sizeof(buf), "%" PRId64 ":%02" PRId64 ":%02" PRId64, hours, minutes, seconds);
		break;
	case TimeFormat::HHMMSS:
		snprintf(buf, sizeof(buf), "%02" PRId64 ":%02" PRId64 ":%02" PRId64, hours, minutes, seconds);
		break;
	case TimeFormat::Auto:
	default:
		if (hours > 0)
			snprintf(buf, sizeof(buf), "%" PRId64 ":%02" PRId64 ":%02" PRId64, hours, minutes, seconds);
		else
			snprintf(buf, sizeof(buf), "%" PRId64 ":%02" PRId64, minutes, seconds);
		break;
	}

	return buf;
}

std::string to_lower(const std::string &str)
{
	std::string out = str;
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return out;
}

std::string trim(const std::string &str)
{
	size_t begin = 0;
	size_t end = str.size();

	while (begin < end && (unsigned char)str[begin] <= ' ')
		begin++;
	while (end > begin && (unsigned char)str[end - 1] <= ' ')
		end--;

	return str.substr(begin, end - begin);
}

bool iequals(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;

	for (size_t i = 0; i < a.size(); i++) {
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
			return false;
	}

	return true;
}

bool is_url(const std::string &path)
{
	static const char *const prefixes[] = {"http://", "https://", "rtmp://", "rtmps://", "rtsp://",
					       "udp://",  "srt://",   "mms://",  "mmsh://",  "ftp://",
					       "file://", "hls://",   "icyx://", "pipe:"};

	const std::string lower = to_lower(path);

	for (const char *prefix : prefixes) {
		if (lower.rfind(prefix, 0) == 0)
			return true;
	}

	return false;
}

std::string file_name(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string file_stem(const std::string &path)
{
	std::string name = file_name(path);
	const size_t dot = name.find_last_of('.');

	if (dot != std::string::npos && dot > 0)
		name = name.substr(0, dot);

	return name;
}

std::string file_extension(const std::string &path)
{
	const std::string name = file_name(path);
	const size_t dot = name.find_last_of('.');

	if (dot == std::string::npos || dot + 1 >= name.size())
		return std::string();

	return to_lower(name.substr(dot + 1));
}

bool is_supported_audio_file(const std::string &path)
{
	static const char *const extensions[] = {"mp3",  "flac", "ogg", "oga", "opus", "m4a",  "m4b",  "mp4a",
						 "aac",  "wav",  "wma", "aif", "aiff", "aifc", "alac", "ape",
						 "mka",  "mpc",  "wv",  "spx", "amr",  "au",   "caf",  "dsf",
						 "it",   "mod",  "s3m", "xm",  "mid",  "midi", "tta",  "ac3",
						 "eac3", "dts",  "mp2", "mpa", "oma",  "ra",   "voc",  "8svx"};

	const std::string ext = file_extension(path);
	if (ext.empty())
		return false;

	for (const char *candidate : extensions) {
		if (ext == candidate)
			return true;
	}

	return false;
}

static void append_utf8(std::string &out, uint32_t cp)
{
	if (cp < 0x80) {
		out.push_back((char)cp);
	} else if (cp < 0x800) {
		out.push_back((char)(0xC0 | (cp >> 6)));
		out.push_back((char)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back((char)(0xE0 | (cp >> 12)));
		out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back((char)(0x80 | (cp & 0x3F)));
	} else {
		out.push_back((char)(0xF0 | (cp >> 18)));
		out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back((char)(0x80 | (cp & 0x3F)));
	}
}

std::string latin1_to_utf8(const uint8_t *data, size_t size)
{
	std::string out;
	out.reserve(size);

	for (size_t i = 0; i < size; i++) {
		if (data[i] == 0)
			break;
		append_utf8(out, data[i]);
	}

	return out;
}

std::string utf16_to_utf8(const uint8_t *data, size_t size, bool big_endian)
{
	std::string out;
	size_t i = 0;

	/* Honour a byte order mark if one is present. */
	if (size >= 2) {
		if (data[0] == 0xFF && data[1] == 0xFE) {
			big_endian = false;
			i = 2;
		} else if (data[0] == 0xFE && data[1] == 0xFF) {
			big_endian = true;
			i = 2;
		}
	}

	for (; i + 1 < size; i += 2) {
		uint32_t unit = big_endian ? (uint32_t)((data[i] << 8) | data[i + 1])
					   : (uint32_t)((data[i + 1] << 8) | data[i]);

		if (unit == 0)
			break;

		if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < size) {
			const uint32_t low = big_endian ? (uint32_t)((data[i + 2] << 8) | data[i + 3])
							: (uint32_t)((data[i + 3] << 8) | data[i + 2]);

			if (low >= 0xDC00 && low <= 0xDFFF) {
				unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
				i += 2;
			}
		}

		append_utf8(out, unit);
	}

	return out;
}

} // namespace vr
