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

#include "audio/audio-format.hpp"
#include "audio/ffmpeg-util.hpp"

#include <string>
#include <vector>

namespace vr {

/*
 * Synchronous audio decoder: one file or stream in, planar float in the
 * engine's format out.
 *
 * Only the best audio stream is read. Everything else in the container, cover
 * art included, is discarded at the demuxer, so a picture stream can never hold
 * up or end playback. Seeks are exact: the decoder lands on the keyframe before
 * the target and drops samples up to it, so the first sample returned after a
 * seek is the one asked for.
 *
 * Not thread safe; StreamDeck runs one on its own thread.
 */
class AudioDecoder {
public:
	enum class Status {
		Ok,          /* samples were appended */
		EndOfStream, /* nothing more; every sample has been returned */
		Error,
	};

	AudioDecoder() = default;
	~AudioDecoder();

	AudioDecoder(const AudioDecoder &) = delete;
	AudioDecoder &operator=(const AudioDecoder &) = delete;

	/* Blocking. `interrupt` must stay valid until close(). */
	bool open(const std::string &path, const AudioFormat &format, const InterruptFn *interrupt,
		  std::string *error = nullptr);
	void close();
	bool is_open() const { return format_ != nullptr; }

	/* 0 when the container does not say. */
	int64_t duration_ms() const { return duration_ms_; }
	bool seekable() const { return seekable_; }

	/* Decodes at least one frame's worth of audio and appends it, one vector
	 * per output channel. `start_frame` receives the position of the first
	 * appended sample, in output samples from the start of the track. */
	Status decode(std::vector<float> *planes, size_t &frames, int64_t &start_frame);

	bool seek(int64_t ms);

	const std::string &last_error() const { return error_; }

private:
	bool configure_resampler(const AVFrame *frame);
	size_t convert(const AVFrame *frame, std::vector<float> *planes);
	void reset_decode_state();

	FormatPtr format_;
	CodecPtr codec_;
	FramePtr frame_;
	PacketPtr packet_;
	SwrPtr swr_;

	AVStream *stream_ = nullptr;
	int stream_index_ = -1;
	int64_t start_pts_ = 0;

	/* Kept to reopen with when a seek fails. */
	std::string path_;
	const InterruptFn *interrupt_ = nullptr;

	AudioFormat out_;
	AVChannelLayout out_layout_ = {};

	/* What the resampler was set up for, so a stream that changes format
	 * part way through gets a new one. */
	int swr_in_rate_ = 0;
	int swr_in_format_ = -1;
	AVChannelLayout swr_in_layout_ = {};

	int64_t duration_ms_ = 0;
	bool seekable_ = false;

	bool input_done_ = false;   /* the demuxer has nothing more */
	bool decoder_done_ = false; /* the codec has been drained */
	bool resampler_done_ = false;

	/* Position of the next sample handed out, and the sample a seek is
	 * waiting for: anything before it is decoded and dropped. */
	int64_t next_frame_ = 0;
	int64_t skip_until_ = -1;
	bool need_sync_ = true;

	std::vector<float> convert_buffer_[MAX_MIX_CHANNELS];
	std::string error_;
};

} // namespace vr
