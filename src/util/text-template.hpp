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

#include "music/snapshot.hpp"
#include "util/vr-util.hpp"

#include <string>

namespace vr {

/* Localised state names are passed in so that this stays free of any OBS
 * dependency and can be exercised on its own. */
struct TemplateContext {
	const Snapshot *snapshot = nullptr;
	bool have_snapshot = false;
	TimeFormat time_format = TimeFormat::Auto;

	const char *state_playing = "Playing";
	const char *state_paused = "Paused";
	const char *state_stopped = "Stopped";
};

/*
 * Expands a now-playing template.
 *
 * {field}  is replaced by that field's value; an unknown name is left as-is so
 *          a typo is visible instead of silently deleting text.
 * [ ... ]  is dropped entirely when any field inside it is empty, so
 *          "{title}[ - {artist}]" loses the dash on an untagged track.
 * \[ \] \{ \} \\ produce those characters literally.
 */
std::string expand_template(const std::string &format, const TemplateContext &context);

} // namespace vr
