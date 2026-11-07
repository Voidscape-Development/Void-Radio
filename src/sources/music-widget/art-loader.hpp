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

#include "render/image-util.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace vr::widget {

/*
 * Decoding a JPEG and blurring it are far too slow for the graphics thread, so
 * they happen on a worker. The widget posts a request whenever the track or
 * the backdrop settings change and picks the result up on a later tick.
 */
class ArtLoader {
public:
	struct Request {
		std::string path;
		std::string placeholder;
		bool want_blur = false;
		uint32_t blur_width = 0;
		uint32_t blur_height = 0;
		float blur_strength = 0.0f;
		float blur_darken = 0.0f;
		float blur_desaturate = 0.0f;
	};

	struct Result {
		RgbaImage art;
		RgbaImage blur;
		uint32_t accent = 0;
		bool accent_valid = false;
		bool have_art = false;
		bool have_blur = false;
	};

	void start()
	{
		if (thread_.joinable())
			return;

		thread_ = std::thread([this]() { run(); });
	}

	void stop()
	{
		if (!thread_.joinable())
			return;

		{
			std::lock_guard<std::mutex> lock(mutex_);
			quit_ = true;
		}

		condition_.notify_all();
		thread_.join();
	}

	void request(const Request &next)
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);

			/* Only the newest request matters; anything queued
			 * behind it is already out of date. */
			pending_ = next;
			have_pending_ = true;
		}

		condition_.notify_one();
	}

	/* Returns true and fills `out` exactly once per completed request. */
	bool take(Result &out)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!have_result_)
			return false;

		out = std::move(result_);
		have_result_ = false;

		return true;
	}

private:
	void run();
	Result process(const Request &request);

	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable condition_;

	bool quit_ = false;
	bool have_pending_ = false;
	Request pending_;

	bool have_result_ = false;
	Result result_;

	/* Worker private: the decoded cover is kept so that changing only the
	 * backdrop settings re-blurs without decoding again. */
	std::string decoded_key_ = "\n";
	RgbaImage decoded_;
	uint32_t decoded_accent_ = 0;
	bool decoded_accent_valid_ = false;
};
} // namespace vr::widget
