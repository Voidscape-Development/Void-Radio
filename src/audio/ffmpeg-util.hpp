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

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <functional>
#include <memory>
#include <string>

namespace vr {

/* Owning handles for the FFmpeg objects the plugin uses. */
struct FormatCloser {
	void operator()(AVFormatContext *ctx) const { avformat_close_input(&ctx); }
};
struct CodecFreer {
	void operator()(AVCodecContext *ctx) const { avcodec_free_context(&ctx); }
};
struct FrameFreer {
	void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};
struct PacketFreer {
	void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
struct SwrFreer {
	void operator()(SwrContext *ctx) const { swr_free(&ctx); }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecFreer>;
using FramePtr = std::unique_ptr<AVFrame, FrameFreer>;
using PacketPtr = std::unique_ptr<AVPacket, PacketFreer>;
using SwrPtr = std::unique_ptr<SwrContext, SwrFreer>;

/* Returns true to abandon whatever blocking I/O is in progress. */
using InterruptFn = std::function<bool()>;

/*
 * Opens a file or URL for reading. Only the container header is read, which is
 * enough for metadata and attached pictures; decoding also needs
 * avformat_find_stream_info. Network inputs get timeouts and reconnects, and
 * `interrupt` (which must outlive the context) lets another thread abandon a
 * stalled read. Returns null on failure, with the reason in `error`.
 */
FormatPtr open_input(const std::string &path, bool is_url, const InterruptFn *interrupt, std::string *error);

std::string av_error_string(int code);

} // namespace vr
