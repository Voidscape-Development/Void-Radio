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

#include <functional>
#include <mutex>
#include <vector>

namespace vr {

/*
 * Work handed to the engine thread from anywhere else. Commands run in the
 * order they were posted, on the engine thread, between blocks of audio - so
 * nothing they touch needs a lock of its own.
 */
class CommandQueue {
public:
	using Command = std::function<void()>;

	void post(Command command)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_.push_back(std::move(command));
	}

	void run_pending()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			running_.swap(pending_);
		}

		for (Command &command : running_)
			command();

		running_.clear();
	}

private:
	std::mutex mutex_;
	std::vector<Command> pending_;
	std::vector<Command> running_;
};

} // namespace vr
