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

#include <obs.h>

namespace vr {

/*
 * Remote control through obs-websocket, as vendor requests under the vendor
 * name "void-radio", so Stream Deck, Streamer.bot, chat bots and anything else
 * that talks obs-websocket can drive the music:
 *
 *   GetMusicSources, GetNowPlaying, GetPlaylist,
 *   Play, Pause, TogglePause, Stop, Next, Previous, Restart,
 *   Seek, PlayIndex, SetShuffle, SetRepeat,
 *   PlayNext, AddToQueue, RemoveFromQueue, ClearQueue
 *
 * Every request takes an optional "sourceName" (or "sourceUuid"); without one
 * it goes to the first music source there is. Every response carries "ok",
 * and "error" when it is false.
 *
 * Events: NowPlayingChanged when a track starts or its details arrive, and
 * PlaybackStateChanged on play, pause and stop.
 *
 * obs-websocket is reached through libobs's proc handlers, so the plugin needs
 * nothing of it to build and simply skips this when it is not installed.
 */

/* From obs_module_post_load, once obs-websocket has loaded. */
void register_websocket_vendor();

/* From a music source's engine thread whenever what it publishes changes.
 * Cheap when nothing changed that an event is sent for. */
void websocket_publish(obs_source_t *source, const Snapshot &snapshot);

/* When a music source goes away. */
void websocket_forget(obs_source_t *source);

} // namespace vr
