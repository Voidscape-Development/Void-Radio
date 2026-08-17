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

#include "music/playlist.hpp"
#include "music/snapshot.hpp"

#include <obs.h>

#include <cstdint>
#include <string>
#include <vector>

/*
 * The surface every other part of the plugin uses to talk to a music source.
 * Callers pass the obs_source_t of a music source; anything else is ignored,
 * so a stale or wrongly typed reference is harmless.
 *
 * The caller must hold a reference to the source (obs_source_get_ref or a
 * strong reference obtained from a weak one) for the duration of the call.
 * Every function here is safe to call from any thread.
 */

namespace vr {

constexpr const char *MUSIC_SOURCE_ID = "void_radio_music_source";
constexpr const char *PROGRESS_BAR_SOURCE_ID = "void_radio_progress_bar";
constexpr const char *MUSIC_WIDGET_SOURCE_ID = "void_radio_music_widget";
constexpr const char *MUSIC_INFO_FILTER_ID = "void_radio_music_info";

struct EntryInfo {
	std::string path;
	std::string title;
	std::string artist;
	bool url = false;
	bool missing = false;
};

bool is_music_source(obs_source_t *source);

/* Returns false when `source` is not a music source. */
bool get_snapshot(obs_source_t *source, Snapshot &out);
bool get_entries(obs_source_t *source, std::vector<EntryInfo> &out, int &current_index);

/* Transport control. */
void transport_play(obs_source_t *source);
void transport_pause(obs_source_t *source);
void transport_toggle_pause(obs_source_t *source);
void transport_stop(obs_source_t *source);
void transport_next(obs_source_t *source);
void transport_previous(obs_source_t *source);
void transport_restart(obs_source_t *source);
void transport_seek(obs_source_t *source, int64_t ms);
void transport_play_index(obs_source_t *source, int index);

/* Playlist and mode changes made from the dock. These write back to the
 * source settings so they survive a scene collection save. */
void set_shuffle(obs_source_t *source, bool shuffle);
void set_repeat(obs_source_t *source, RepeatMode mode);
void set_entry_override(obs_source_t *source, const std::string &path, const std::string &title,
			const std::string &artist);
void set_entry_order(obs_source_t *source, const std::vector<std::string> &paths);
void remove_entry(obs_source_t *source, const std::string &path);

/* Fills `out` with a strong reference to every music source in the current
 * scene collection. The caller owns the references and must release them. */
void enum_music_sources(std::vector<obs_source_t *> &out);

} // namespace vr
