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

namespace vr {

/*
 * "Music Reactive": a video filter for any source that makes it move with the
 * music playing in a Music Source. It follows the beat, the bass, the treble
 * or the overall level, and can pulse the source's size, brighten it, flash
 * it towards a colour, fade it, or shake it.
 */
void register_music_reactive_filter();

} // namespace vr
