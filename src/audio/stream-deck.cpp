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

#include "audio/stream-deck.hpp"

#include <algorithm>
#include <chrono>

namespace vr {

namespace {

constexpr int64_t BUFFER_AHEAD_MS = 500;
constexpr int64_t BUFFER_SLACK_MS = 1500;
constexpr int STREAM_RETRY_SECONDS = 5;

} // namespace

StreamDeck::StreamDeck(std::string path, bool is_url, const AudioFormat &format, LogFn log)
	: path_(std::move(path)),
	  is_url_(is_url),
	  format_(format),
	  log_(std::move(log)),
	  target_frames_(ms_to_frames(BUFFER_AHEAD_MS, format.sample_rate)),
	  capacity_frames_(ms_to_frames(BUFFER_AHEAD_MS + BUFFER_SLACK_MS, format.sample_rate))
{
	interrupt_ = [this]() {
		return abort_.load();
	};

	for (uint32_t ch = 0; ch < format_.channels && ch < MAX_MIX_CHANNELS; ch++)
		fifo_[ch].reset(capacity_frames_);

	thread_ = std::thread([this]() { run(); });
}

StreamDeck::~StreamDeck()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		abort_.store(true);
	}
	cv_.notify_all();

	if (thread_.joinable())
		thread_.join();
}

void StreamDeck::clear_buffer_locked()
{
	for (uint32_t ch = 0; ch < format_.channels; ch++)
		fifo_[ch].clear();
}

Deck::State StreamDeck::state() const
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (buffered_locked() > 0)
		return State::Ready;
	if (failed_)
		return State::Failed;
	if (!opened_)
		return State::Opening;
	if (eof_ && !seek_pending_)
		return State::Ended;
	return State::Ready;
}

size_t StreamDeck::read(float *const *out, size_t frames)
{
	size_t got = 0;

	{
		std::lock_guard<std::mutex> lock(mutex_);

		got = std::min(frames, buffered_locked());
		for (uint32_t ch = 0; ch < format_.channels; ch++)
			fifo_[ch].pop(out[ch], got);

		head_frame_ += (int64_t)got;
		if (got)
			produced_ = true;
	}

	if (got)
		cv_.notify_all();

	return got;
}

void StreamDeck::seek(int64_t ms)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!seekable_)
			return;

		if (ms < 0)
			ms = 0;
		if (duration_ms_ > 0 && ms > duration_ms_)
			ms = duration_ms_;

		clear_buffer_locked();
		head_frame_ = (int64_t)ms_to_frames(ms, format_.sample_rate);
		eof_ = false;
		seek_pending_ = true;
		seek_target_ms_ = ms;
		seek_generation_++;
	}

	cv_.notify_all();
}

int64_t StreamDeck::position_ms() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return frames_to_ms(head_frame_, format_.sample_rate);
}

int64_t StreamDeck::duration_ms() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return duration_ms_;
}

bool StreamDeck::seekable() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return seekable_;
}

bool StreamDeck::produced() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return produced_;
}

bool StreamDeck::open_decoder(AudioDecoder &decoder)
{
	std::string error;
	if (decoder.open(path_, is_url_, format_, &interrupt_, &error))
		return true;

	if (log_ && !abort_.load())
		log_("could not play '" + path_ + "': " + error);

	return false;
}

void StreamDeck::wait_for_retry()
{
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait_for(lock, std::chrono::seconds(STREAM_RETRY_SECONDS), [this]() { return abort_.load(); });
}

void StreamDeck::run()
{
	AudioDecoder decoder;

	if (!open_decoder(decoder)) {
		std::lock_guard<std::mutex> lock(mutex_);
		failed_ = true;
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		opened_ = true;
		duration_ms_ = decoder.duration_ms();
		seekable_ = decoder.seekable();
	}

	std::vector<float> planes[MAX_MIX_CHANNELS];

	for (;;) {
		bool do_seek = false;
		int64_t seek_ms = 0;
		uint64_t generation;

		{
			std::unique_lock<std::mutex> lock(mutex_);

			cv_.wait(lock, [this]() {
				return abort_.load() || seek_pending_ ||
				       (!eof_ && !failed_ && buffered_locked() < target_frames_);
			});

			if (abort_.load())
				return;

			if (seek_pending_) {
				seek_pending_ = false;
				do_seek = true;
				seek_ms = seek_target_ms_;
			}

			generation = seek_generation_;
		}

		if (do_seek) {
			const bool ok = decoder.seek(seek_ms);

			std::lock_guard<std::mutex> lock(mutex_);
			if (!ok && log_)
				log_("seek failed in '" + path_ + "': " + decoder.last_error());
			continue;
		}

		for (uint32_t ch = 0; ch < format_.channels; ch++)
			planes[ch].clear();

		size_t frames = 0;
		int64_t start_frame = 0;
		const AudioDecoder::Status status = decoder.decode(planes, frames, start_frame);

		bool reconnect = false;

		{
			std::unique_lock<std::mutex> lock(mutex_);

			/* A seek arrived while decoding: this audio belongs to the
			 * position that was left. */
			if (generation != seek_generation_)
				continue;

			if (frames) {
				if (buffered_locked() == 0)
					head_frame_ = start_frame;

				/* The capacity leaves room for a whole chunk on top
				 * of the target, so this only waits in the odd case
				 * of an enormous one. */
				size_t offset = 0;
				while (offset < frames && !abort_.load() && generation == seek_generation_) {
					const size_t fit = std::min(frames - offset, fifo_[0].space());
					for (uint32_t ch = 0; ch < format_.channels; ch++)
						fifo_[ch].push(planes[ch].data() + offset, fit);
					offset += fit;

					if (offset < frames)
						cv_.wait(lock, [&]() {
							return abort_.load() || generation != seek_generation_ ||
							       fifo_[0].space() > 0;
						});
				}
			}

			if (status == AudioDecoder::Status::EndOfStream) {
				eof_ = true;
			} else if (status == AudioDecoder::Status::Error) {
				if (is_url_ && produced_) {
					reconnect = true;
				} else {
					/* A file that played and then hit damage ends
					 * there; one that never played failed. */
					if (produced_)
						eof_ = true;
					else
						failed_ = true;

					if (log_)
						log_("stopped reading '" + path_ + "': " + decoder.last_error());
				}
			}
		}

		if (!reconnect)
			continue;

		if (log_)
			log_("stream dropped, reconnecting: " + path_);

		decoder.close();

		while (!abort_.load()) {
			wait_for_retry();
			if (abort_.load())
				return;
			if (open_decoder(decoder))
				break;
		}
	}
}

} // namespace vr
