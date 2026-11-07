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

#include "audio/audio-decoder.hpp"
#include "audio/deck.hpp"
#include "audio/ring-buffer.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace vr {

/*
 * A Deck backed by an AudioDecoder on its own thread.
 *
 * The thread opens the file (which for a URL can take seconds), then keeps a
 * short buffer of decoded audio ahead of playback. The engine thread only ever
 * takes from that buffer, so it never waits on the disk or the network.
 *
 * A seek clears the buffer at once and hands the new position to the thread;
 * anything the thread decodes for the old position after that is thrown away.
 * A stream that drops after it has played is reopened rather than ended.
 */
class StreamDeck final : public Deck {
public:
	using LogFn = std::function<void(const std::string &)>;

	StreamDeck(std::string path, bool is_url, const AudioFormat &format, LogFn log = {});
	~StreamDeck() override;

	StreamDeck(const StreamDeck &) = delete;
	StreamDeck &operator=(const StreamDeck &) = delete;

	State state() const override;
	size_t read(float *const *out, size_t frames) override;
	void seek(int64_t ms) override;
	int64_t position_ms() const override;
	int64_t duration_ms() const override;
	bool seekable() const override;
	bool produced() const override;

private:
	void run();
	bool open_decoder(AudioDecoder &decoder);
	void wait_for_retry();
	void clear_buffer_locked();
	size_t buffered_locked() const { return fifo_[0].size(); }

	const std::string path_;
	const bool is_url_;
	const AudioFormat format_;
	const LogFn log_;

	/* How far ahead the thread decodes, and the hard size of the buffer,
	 * which has room for a whole decoded chunk on top of the target. */
	const size_t target_frames_;
	const size_t capacity_frames_;

	std::atomic<bool> abort_{false};
	InterruptFn interrupt_;

	mutable std::mutex mutex_;
	std::condition_variable cv_;

	RingBuffer fifo_[MAX_MIX_CHANNELS];
	int64_t head_frame_ = 0; /* position of the first buffered sample */

	bool opened_ = false;
	bool eof_ = false;
	bool failed_ = false;
	bool produced_ = false;
	int64_t duration_ms_ = 0;
	bool seekable_ = false;

	bool seek_pending_ = false;
	int64_t seek_target_ms_ = 0;
	uint64_t seek_generation_ = 0;

	std::thread thread_;
};

} // namespace vr
