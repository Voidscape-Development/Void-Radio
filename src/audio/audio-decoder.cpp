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

#include "audio/audio-decoder.hpp"

#include <algorithm>

namespace vr {

namespace {

/* Corrupt packets are skipped rather than ending the track, but a file that is
 * nothing but corruption has to give up eventually. */
constexpr int MAX_CONSECUTIVE_BAD_PACKETS = 64;

void layout_from_mask(AVChannelLayout &layout, uint64_t mask, uint32_t channels)
{
	av_channel_layout_uninit(&layout);

	if (mask && av_channel_layout_from_mask(&layout, mask) == 0 && layout.nb_channels == (int)channels)
		return;

	av_channel_layout_uninit(&layout);
	av_channel_layout_default(&layout, (int)channels);
}

} // namespace

AudioDecoder::~AudioDecoder()
{
	close();
}

void AudioDecoder::close()
{
	swr_.reset();
	codec_.reset();
	format_.reset();
	frame_.reset();
	packet_.reset();
	stream_ = nullptr;
	stream_index_ = -1;
	duration_ms_ = 0;
	seekable_ = false;

	av_channel_layout_uninit(&out_layout_);
	av_channel_layout_uninit(&swr_in_layout_);
	swr_in_rate_ = 0;
	swr_in_format_ = -1;

	reset_decode_state();
	next_frame_ = 0;
}

void AudioDecoder::reset_decode_state()
{
	input_done_ = false;
	decoder_done_ = false;
	resampler_done_ = false;
	skip_until_ = -1;
	need_sync_ = true;
}

bool AudioDecoder::open(const std::string &path, bool is_url, const AudioFormat &format, const InterruptFn *interrupt,
			std::string *error)
{
	close();

	const auto fail = [&](const std::string &reason) {
		error_ = reason;
		if (error)
			*error = reason;
		close();
		return false;
	};

	if (!format.channels || format.channels > MAX_MIX_CHANNELS || !format.sample_rate)
		return fail("unsupported output format");

	out_ = format;
	path_ = path;
	is_url_ = is_url;
	interrupt_ = interrupt;
	layout_from_mask(out_layout_, format.channel_mask, format.channels);

	std::string open_error;
	format_ = open_input(path, is_url, interrupt, &open_error);
	if (!format_)
		return fail("could not open: " + open_error);

	/* Cover art and any video are dropped at the demuxer before anything is
	 * probed, so they are never decoded and can never be what decides that
	 * the file has ended. */
	for (unsigned i = 0; i < format_->nb_streams; i++) {
		if (format_->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
			format_->streams[i]->discard = AVDISCARD_ALL;
	}

	int ret = avformat_find_stream_info(format_.get(), nullptr);
	if (ret < 0)
		return fail("could not read stream info: " + av_error_string(ret));

	const AVCodec *codec = nullptr;
	stream_index_ = av_find_best_stream(format_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (stream_index_ < 0 || !codec)
		return fail("no audio stream");

	/* Only the chosen stream is read; a second audio track is dropped too. */
	for (unsigned i = 0; i < format_->nb_streams; i++) {
		if ((int)i != stream_index_)
			format_->streams[i]->discard = AVDISCARD_ALL;
	}

	stream_ = format_->streams[stream_index_];

	codec_.reset(avcodec_alloc_context3(codec));
	if (!codec_)
		return fail("out of memory");

	ret = avcodec_parameters_to_context(codec_.get(), stream_->codecpar);
	if (ret < 0)
		return fail("bad codec parameters: " + av_error_string(ret));

	codec_->pkt_timebase = stream_->time_base;

	ret = avcodec_open2(codec_.get(), codec, nullptr);
	if (ret < 0)
		return fail("could not open decoder: " + av_error_string(ret));

	frame_.reset(av_frame_alloc());
	packet_.reset(av_packet_alloc());
	if (!frame_ || !packet_)
		return fail("out of memory");

	start_pts_ = stream_->start_time != AV_NOPTS_VALUE ? stream_->start_time : 0;

	if (format_->duration != AV_NOPTS_VALUE && format_->duration > 0)
		duration_ms_ = format_->duration / (AV_TIME_BASE / 1000);
	else if (stream_->duration != AV_NOPTS_VALUE && stream_->duration > 0)
		duration_ms_ = av_rescale_q(stream_->duration, stream_->time_base, AVRational{1, 1000});

	seekable_ = duration_ms_ > 0 && format_->pb && (format_->pb->seekable & AVIO_SEEKABLE_NORMAL);

	reset_decode_state();
	next_frame_ = 0;
	error_.clear();
	return true;
}

bool AudioDecoder::configure_resampler(const AVFrame *frame)
{
	AVChannelLayout in_layout = {};

	if (frame->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC && frame->ch_layout.nb_channels > 0)
		av_channel_layout_copy(&in_layout, &frame->ch_layout);
	else
		av_channel_layout_default(&in_layout, frame->ch_layout.nb_channels > 0 ? frame->ch_layout.nb_channels
										       : codec_->ch_layout.nb_channels);

	const bool same = swr_ && swr_in_rate_ == frame->sample_rate && swr_in_format_ == frame->format &&
			  av_channel_layout_compare(&swr_in_layout_, &in_layout) == 0;

	if (same) {
		av_channel_layout_uninit(&in_layout);
		return true;
	}

	SwrContext *raw = nullptr;
	int ret = swr_alloc_set_opts2(&raw, &out_layout_, AV_SAMPLE_FMT_FLTP, (int)out_.sample_rate, &in_layout,
				      (AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr);
	swr_.reset(raw);

	if (ret >= 0)
		ret = swr_init(swr_.get());

	av_channel_layout_uninit(&swr_in_layout_);
	swr_in_layout_ = in_layout;
	swr_in_rate_ = frame->sample_rate;
	swr_in_format_ = frame->format;

	if (ret < 0) {
		error_ = "could not set up resampling: " + av_error_string(ret);
		swr_.reset();
		return false;
	}

	return true;
}

size_t AudioDecoder::convert(const AVFrame *frame, std::vector<float> *planes)
{
	if (!swr_)
		return 0;

	const int in_samples = frame ? frame->nb_samples : 0;
	const int capacity = swr_get_out_samples(swr_.get(), in_samples);
	if (capacity <= 0)
		return 0;

	uint8_t *out[MAX_MIX_CHANNELS] = {};
	for (uint32_t ch = 0; ch < out_.channels; ch++) {
		convert_buffer_[ch].resize((size_t)capacity);
		out[ch] = reinterpret_cast<uint8_t *>(convert_buffer_[ch].data());
	}

	const int converted = swr_convert(swr_.get(), out, capacity,
					  frame ? const_cast<const uint8_t **>(frame->extended_data) : nullptr,
					  in_samples);
	if (converted <= 0)
		return 0;

	/* Drop whatever lies before a seek target. */
	size_t drop = 0;
	if (skip_until_ >= 0) {
		if (skip_until_ > next_frame_)
			drop = (size_t)std::min<int64_t>(skip_until_ - next_frame_, converted);
		if (drop < (size_t)converted)
			skip_until_ = -1;
	}

	next_frame_ += converted;

	const size_t keep = (size_t)converted - drop;
	for (uint32_t ch = 0; ch < out_.channels; ch++)
		planes[ch].insert(planes[ch].end(), convert_buffer_[ch].begin() + (ptrdiff_t)drop,
				  convert_buffer_[ch].begin() + converted);

	return keep;
}

AudioDecoder::Status AudioDecoder::decode(std::vector<float> *planes, size_t &frames, int64_t &start_frame)
{
	frames = 0;
	start_frame = next_frame_;

	if (!is_open())
		return Status::Error;

	int bad_packets = 0;

	for (;;) {
		if (decoder_done_) {
			/* Whatever the resampler is still holding goes out last. */
			if (!resampler_done_) {
				resampler_done_ = true;
				const size_t flushed = convert(nullptr, planes);
				if (flushed) {
					start_frame = next_frame_ - (int64_t)flushed;
					frames = flushed;
					return Status::Ok;
				}
			}

			return Status::EndOfStream;
		}

		int ret = avcodec_receive_frame(codec_.get(), frame_.get());

		if (ret == 0) {
			AVFrame *frame = frame_.get();

			if (need_sync_ && frame->best_effort_timestamp != AV_NOPTS_VALUE) {
				const int64_t pts = frame->best_effort_timestamp - start_pts_;
				next_frame_ = std::max<int64_t>(0, av_rescale_q(pts, stream_->time_base,
										AVRational{1, (int)out_.sample_rate}));
			}
			need_sync_ = false;

			if (!configure_resampler(frame)) {
				av_frame_unref(frame);
				return Status::Error;
			}

			const size_t got = convert(frame, planes);
			av_frame_unref(frame);

			if (got) {
				/* Anything dropped for a seek came off the front. */
				start_frame = next_frame_ - (int64_t)got;
				frames = got;
				return Status::Ok;
			}

			continue;
		}

		if (ret == AVERROR_EOF) {
			decoder_done_ = true;
			continue;
		}

		if (ret != AVERROR(EAGAIN)) {
			error_ = "decoding failed: " + av_error_string(ret);
			return Status::Error;
		}

		/* The decoder wants more input. */
		if (input_done_) {
			decoder_done_ = true;
			continue;
		}

		ret = av_read_frame(format_.get(), packet_.get());

		if (ret == AVERROR_EOF || (ret < 0 && format_->pb && avio_feof(format_->pb))) {
			input_done_ = true;
			avcodec_send_packet(codec_.get(), nullptr);
			continue;
		}

		if (ret < 0) {
			error_ = "reading failed: " + av_error_string(ret);
			return Status::Error;
		}

		if (packet_->stream_index != stream_index_) {
			av_packet_unref(packet_.get());
			continue;
		}

		ret = avcodec_send_packet(codec_.get(), packet_.get());
		av_packet_unref(packet_.get());

		if (ret == AVERROR_INVALIDDATA) {
			if (++bad_packets > MAX_CONSECUTIVE_BAD_PACKETS) {
				error_ = "too much corrupt data";
				return Status::Error;
			}
			continue;
		}

		if (ret < 0 && ret != AVERROR(EAGAIN)) {
			error_ = "decoding failed: " + av_error_string(ret);
			return Status::Error;
		}

		bad_packets = 0;
	}
}

bool AudioDecoder::seek(int64_t ms)
{
	if (!is_open())
		return false;

	if (ms < 0)
		ms = 0;

	const int64_t target = start_pts_ + av_rescale_q(ms, AVRational{1, 1000}, stream_->time_base);

	/* The keyframe at or before the target, which every demuxer can find;
	 * failing that (some FLAC files without a seek table give up near the
	 * end), the nearest one either side. Samples before the target are
	 * dropped below, and the position corrects itself from the first
	 * decoded frame if the demuxer lands after it. */
	int ret = avformat_seek_file(format_.get(), stream_index_, INT64_MIN, target, target, 0);
	if (ret < 0)
		ret = av_seek_frame(format_.get(), stream_index_, target, AVSEEK_FLAG_BACKWARD);
	if (ret < 0)
		ret = avformat_seek_file(format_.get(), stream_index_, INT64_MIN, target, INT64_MAX, 0);

	if (ret < 0) {
		/* A failed seek can leave the demuxer anywhere, often at the end
		 * of the file. Start again from the top and decode forward to
		 * the target instead: slower, but it always lands. */
		const std::string reason = av_error_string(ret);
		const std::string path = path_;
		const InterruptFn *interrupt = interrupt_;

		if (!open(path, is_url_, out_, interrupt)) {
			error_ = "seek failed (" + reason + ") and the file could not be reopened";
			return false;
		}

		skip_until_ = (int64_t)ms_to_frames(ms, out_.sample_rate);
		return true;
	}

	avcodec_flush_buffers(codec_.get());

	/* The resampler's history belongs to the old position. */
	swr_.reset();
	swr_in_rate_ = 0;
	swr_in_format_ = -1;
	av_channel_layout_uninit(&swr_in_layout_);

	reset_decode_state();
	next_frame_ = (int64_t)ms_to_frames(ms, out_.sample_rate);
	skip_until_ = next_frame_;
	return true;
}

} // namespace vr
