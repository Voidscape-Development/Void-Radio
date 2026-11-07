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

/*
 * The Music Widget layout editor: a window showing a widget as OBS draws it,
 * where its art, lines and progress bar can be dragged and resized directly.
 * Opened from a widget's properties and from OBS's Tools menu.
 */

/* Opens the editor on `widget`, or brings it forward and switches to it if it
 * is already open. With no widget it picks the one selected in the current
 * scene, or failing that the first one there is. */
void open_widget_editor(obs_source_t *widget);

/* Adds the Tools menu entry. Called once the frontend is up. */
void register_widget_editor();

/* Closes the editor if it is open. Called on unload. */
void close_widget_editor();

} // namespace vr
