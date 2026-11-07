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

#include <obs.h>

#include <memory>
#include <vector>

namespace vr {

class MusicSource;

/* The per-source transport hotkeys, registered for as long as this lives. */
class SourceHotkeys {
public:
	explicit SourceHotkeys(MusicSource *music);
	~SourceHotkeys();

	SourceHotkeys(const SourceHotkeys &) = delete;
	SourceHotkeys &operator=(const SourceHotkeys &) = delete;

	struct Binding;

private:
	std::vector<std::unique_ptr<Binding>> bindings_;
};

} // namespace vr
