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
#include <vector>

namespace vr {

/* A fixed-capacity FIFO of float samples for one channel. Not thread safe on
 * its own; the owner provides the locking. */
class RingBuffer {
public:
	void reset(size_t capacity);
	void clear();

	size_t size() const { return size_; }
	size_t capacity() const { return data_.size(); }
	size_t space() const { return data_.size() - size_; }

	/* Appends up to `count` samples and returns how many fit. Nothing
	 * already buffered is ever overwritten. */
	size_t push(const float *src, size_t count);

	/* Removes up to `count` samples into `dst` and returns how many. */
	size_t pop(float *dst, size_t count);

private:
	std::vector<float> data_;
	size_t head_ = 0;
	size_t size_ = 0;
};

} // namespace vr
