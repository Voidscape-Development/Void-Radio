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

#include "audio/ffmpeg-util.hpp"

namespace vr {

namespace {

int interrupt_trampoline(void *opaque)
{
	const InterruptFn *fn = static_cast<const InterruptFn *>(opaque);
	return fn && *fn && (*fn)() ? 1 : 0;
}

} // namespace

std::string av_error_string(int code)
{
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(code, buffer, sizeof(buffer));
	return buffer;
}

FormatPtr open_input(const std::string &path, const InterruptFn *interrupt, std::string *error)
{
	AVFormatContext *raw = avformat_alloc_context();
	if (!raw) {
		if (error)
			*error = "out of memory";
		return nullptr;
	}

	if (interrupt) {
		raw->interrupt_callback.callback = interrupt_trampoline;
		raw->interrupt_callback.opaque = const_cast<InterruptFn *>(interrupt);
	}

	/* On failure avformat_open_input frees the context itself. */
	const int ret = avformat_open_input(&raw, path.c_str(), nullptr, nullptr);

	if (ret < 0) {
		if (error)
			*error = av_error_string(ret);
		return nullptr;
	}

	return FormatPtr(raw);
}

} // namespace vr
