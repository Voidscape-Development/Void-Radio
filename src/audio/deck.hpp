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

#include <cstddef>
#include <cstdint>

namespace vr {

/*
 * One track being played: a source of decoded audio in the engine's format.
 *
 * The player only ever talks to decks through this interface, which is what
 * lets its transport logic be tested against fake decks with no files, threads
 * or clocks involved. StreamDeck is the real implementation.
 *
 * All calls come from the engine thread.
 */
class Deck {
public:
	enum class State {
		Opening, /* not ready yet; reads return nothing */
		Ready,   /* playing; a short read just means the decoder is behind */
		Ended,   /* every sample has been read */
		Failed,  /* could not be opened or decoded */
	};

	virtual ~Deck() = default;

	virtual State state() const = 0;

	/* Fills up to `frames` samples into each of the planar channel buffers
	 * and returns how many were written. Never blocks. */
	virtual size_t read(float *const *out, size_t frames) = 0;

	/* Moves playback. Reads return nothing until the decoder has caught up
	 * with the new position, and the deck never reports Ended or Failed
	 * for the position it is leaving. */
	virtual void seek(int64_t ms) = 0;

	/* Position of the next sample read() will return. Exact, because it is
	 * counted in samples rather than taken from packet timestamps. */
	virtual int64_t position_ms() const = 0;

	/* 0 when unknown, as for a live stream. */
	virtual int64_t duration_ms() const = 0;

	virtual bool seekable() const = 0;

	/* Whether any audio has come out of this deck. One that ends without
	 * ever producing any counts as a failure. */
	virtual bool produced() const = 0;
};

} // namespace vr
