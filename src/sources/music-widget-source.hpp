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

namespace vr {

void register_music_widget_source();

/*
 * For the layout editor. Both are safe to call from the UI thread and ignore
 * anything that is not a Music Widget.
 *
 * The sample track stands in for the linked music source while the editor is
 * designing, so there is something to lay out with nothing playing. It is what
 * the widget draws everywhere, OBS's own preview and output included, until it
 * is switched off again.
 */
void set_widget_sample_track(obs_source_t *widget, bool active, bool long_text);

/* Plays the track change again: with the sample track on, moves to the other
 * sample track; otherwise runs the pop-in, entrances and transition as if a
 * track had just started. */
void replay_widget_track_change(obs_source_t *widget);

} // namespace vr
