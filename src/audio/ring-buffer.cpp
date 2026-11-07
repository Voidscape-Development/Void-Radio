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

#include "audio/ring-buffer.hpp"

#include <algorithm>
#include <cstring>

namespace vr {

void RingBuffer::reset(size_t capacity)
{
	data_.assign(capacity, 0.0f);
	head_ = 0;
	size_ = 0;
}

void RingBuffer::clear()
{
	head_ = 0;
	size_ = 0;
}

size_t RingBuffer::push(const float *src, size_t count)
{
	const size_t capacity = data_.size();
	count = std::min(count, space());
	if (!count)
		return 0;

	const size_t tail = (head_ + size_) % capacity;
	const size_t first = std::min(count, capacity - tail);

	memcpy(&data_[tail], src, first * sizeof(float));
	if (count > first)
		memcpy(&data_[0], src + first, (count - first) * sizeof(float));

	size_ += count;
	return count;
}

size_t RingBuffer::pop(float *dst, size_t count)
{
	const size_t capacity = data_.size();
	const size_t take = std::min(count, size_);
	if (!take)
		return 0;

	const size_t first = std::min(take, capacity - head_);

	memcpy(dst, &data_[head_], first * sizeof(float));
	if (take > first)
		memcpy(dst + first, &data_[0], (take - first) * sizeof(float));

	head_ = (head_ + take) % capacity;
	size_ -= take;
	return take;
}

} // namespace vr
