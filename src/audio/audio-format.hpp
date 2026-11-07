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

namespace vr {

/* Maximum channel count the engine mixes, matching OBS's own limit. */
constexpr uint32_t MAX_MIX_CHANNELS = 8;

/*
 * The format everything in the playback engine works in: planar 32-bit float
 * at the output's sample rate and channel count. Decoders convert into it, so
 * mixing never has to think about formats.
 *
 * `channel_mask` uses FFmpeg's AV_CH_* bit layout. Zero means "the default
 * layout for this many channels".
 */
struct AudioFormat {
	uint32_t sample_rate = 48000;
	uint32_t channels = 2;
	uint64_t channel_mask = 0;
};

inline uint64_t ms_to_frames(int64_t ms, uint32_t rate)
{
	return ms <= 0 ? 0 : (uint64_t)ms * rate / 1000;
}

inline int64_t frames_to_ms(int64_t frames, uint32_t rate)
{
	return rate ? frames * 1000 / (int64_t)rate : 0;
}

} // namespace vr
