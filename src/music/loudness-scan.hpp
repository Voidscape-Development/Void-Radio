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

#include "audio/ffmpeg-util.hpp"

#include <string>

namespace vr {

/*
 * Measures a whole file's integrated loudness by decoding all of it, for
 * tracks that carry no ReplayGain or R128 tags. Runs on the library worker; a
 * four minute track takes about a second. `interrupt` may be null; when it
 * returns true the measurement stops and fails.
 */
bool measure_loudness(const std::string &path, float &loudness, float &peak, const InterruptFn *interrupt = nullptr);

/*
 * Measured results, kept on disk so a library is only ever measured once. A
 * file is known by its path, size and modification time, so editing it
 * measures it again. Safe from any thread.
 */
bool cached_loudness(const std::string &path, float &loudness, float &peak);
void store_loudness(const std::string &path, float loudness, float peak);

/* Where the cache lives. Set once from the module; tests leave it unset,
 * which keeps the cache in memory only. */
void set_loudness_cache_file(const std::string &path);

} // namespace vr
