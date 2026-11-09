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

#include "music/loudness-scan.hpp"
#include "audio/audio-decoder.hpp"
#include "audio/loudness.hpp"

#include <util/platform.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <sys/stat.h>

namespace vr {

bool measure_loudness(const std::string &path, float &loudness, float &peak, const InterruptFn *interrupt)
{
	/* Measured at 48 kHz stereo whatever the file is: loudness does not
	 * depend on the rate, and it keeps the meter one shape. */
	const AudioFormat format{48000, 2, 0};

	AudioDecoder decoder;
	if (!decoder.open(path, false, format, interrupt))
		return false;

	LoudnessMeter meter(format.sample_rate, format.channels);
	std::vector<float> planes[MAX_MIX_CHANNELS];

	for (;;) {
		if (interrupt && *interrupt && (*interrupt)())
			return false;

		size_t frames = 0;
		int64_t start = 0;
		const AudioDecoder::Status status = decoder.decode(planes, frames, start);

		if (frames) {
			const float *in[2] = {planes[0].data(), planes[1].data()};
			meter.add(in, planes[0].size());
		}
		planes[0].clear();
		planes[1].clear();

		if (status == AudioDecoder::Status::EndOfStream)
			break;
		if (status == AudioDecoder::Status::Error)
			return false;
	}

	const double integrated = meter.integrated();
	if (std::isnan(integrated))
		return false;

	loudness = (float)integrated;
	peak = (float)meter.peak();
	return true;
}

/* -------------------------------------------------------------------------- */
/* Cache                                                                       */
/* -------------------------------------------------------------------------- */

namespace {

struct Entry {
	long long size = 0;
	long long mtime = 0;
	float loudness = NAN;
	float peak = 0.0f;
};

struct Cache {
	std::mutex mutex;
	std::string file;
	bool loaded = false;
	std::map<std::string, Entry> entries;
};

Cache &cache()
{
	static Cache instance;
	return instance;
}

bool stat_file(const std::string &path, long long &size, long long &mtime)
{
	struct stat info = {};
	if (os_stat(path.c_str(), &info) != 0)
		return false;
	size = (long long)info.st_size;
	mtime = (long long)info.st_mtime;
	return true;
}

/*
 * One line per file, tab separated: size, mtime, loudness, peak, path. The
 * path goes last so it can hold anything but a newline, which no file name
 * in a music library does.
 */
void load_locked(Cache &c)
{
	if (c.loaded)
		return;
	c.loaded = true;

	if (c.file.empty())
		return;

	/* os_fopen takes UTF-8 on every platform, which plain fopen does not
	 * on Windows. */
	FILE *in = os_fopen(c.file.c_str(), "rb");
	if (!in)
		return;

	char line[8192];
	while (std::fgets(line, sizeof(line), in)) {
		char *end = line + std::strlen(line);
		while (end > line && (end[-1] == '\n' || end[-1] == '\r'))
			*--end = '\0';

		Entry entry;
		char *at = line;
		char *next = nullptr;

		entry.size = std::strtoll(at, &next, 10);
		if (next == at || *next != '\t')
			continue;
		at = next + 1;
		entry.mtime = std::strtoll(at, &next, 10);
		if (next == at || *next != '\t')
			continue;
		at = next + 1;
		entry.loudness = std::strtof(at, &next);
		if (next == at || *next != '\t')
			continue;
		at = next + 1;
		entry.peak = std::strtof(at, &next);
		if (next == at || *next != '\t')
			continue;

		const std::string path(next + 1);
		if (!path.empty() && std::isfinite(entry.loudness))
			c.entries[path] = entry;
	}

	std::fclose(in);
}

void save_locked(const Cache &c)
{
	if (c.file.empty())
		return;

	/* Written aside and moved into place, so a crash never leaves half a
	 * cache behind. */
	const std::string temp = c.file + ".tmp";
	FILE *out = os_fopen(temp.c_str(), "wb");
	if (!out)
		return;

	for (const auto &[path, entry] : c.entries)
		std::fprintf(out, "%lld\t%lld\t%.3f\t%.6f\t%s\n", entry.size, entry.mtime, entry.loudness, entry.peak,
			     path.c_str());

	std::fclose(out);
	os_unlink(c.file.c_str());
	os_rename(temp.c_str(), c.file.c_str());
}

} // namespace

void set_loudness_cache_file(const std::string &path)
{
	Cache &c = cache();
	std::lock_guard<std::mutex> lock(c.mutex);
	c.file = path;
	c.loaded = false;
	c.entries.clear();
}

bool cached_loudness(const std::string &path, float &loudness, float &peak)
{
	long long size = 0, mtime = 0;
	if (!stat_file(path, size, mtime))
		return false;

	Cache &c = cache();
	std::lock_guard<std::mutex> lock(c.mutex);
	load_locked(c);

	const auto it = c.entries.find(path);
	if (it == c.entries.end() || it->second.size != size || it->second.mtime != mtime)
		return false;

	loudness = it->second.loudness;
	peak = it->second.peak;
	return true;
}

void store_loudness(const std::string &path, float loudness, float peak)
{
	long long size = 0, mtime = 0;
	if (!stat_file(path, size, mtime) || !std::isfinite(loudness))
		return;

	Cache &c = cache();
	std::lock_guard<std::mutex> lock(c.mutex);
	load_locked(c);

	c.entries[path] = Entry{size, mtime, loudness, peak};
	save_locked(c);
}

} // namespace vr
