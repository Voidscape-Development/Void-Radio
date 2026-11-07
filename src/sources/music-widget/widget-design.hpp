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

#include "sources/music-widget/widget-layout.hpp"

#include <obs-data.h>

#include <string>
#include <vector>

namespace vr::widget {

/*
 * The widget's design settings - layout, style and lines - and how they turn
 * into the detailed per-element settings the renderer has always read.
 *
 * Nothing downstream knows layouts or styles exist: resolve_settings() takes
 * what the user set and returns a copy with every element's position, size,
 * font and colours filled in. The renderer reads that copy exactly as it read
 * hand-set values before, and the layout editor uses the same copy to know
 * where everything is drawn.
 */

/* Stored in settings; the numbers must not change. */
enum class WidgetStyle {
	Custom = 0, /* every element styled by hand, as before styles existed */
	Glass = 1,
	Solid = 2,
	Minimal = 3,
	Outline = 4,
	Vinyl = 5,
};

enum class ColorSource {
	Album = 0,
	Fixed = 1,
};

enum class ArtShape {
	FromStyle = 0,
	Square = 1,
	Rounded = 2,
	Circle = 3,
};

enum class LongText {
	Scroll = 0,
	Shrink = 1,
	Truncate = 2,
};

/* Lines on the main view; the rest live in the advanced options. */
constexpr int MAIN_LINES = 4;

void add_design_defaults(obs_data_t *settings);

/* Widgets saved before layouts and styles existed have none of their keys.
 * They are put on Free placement and the Custom style, which draw exactly what
 * they drew before. A brand new widget has no saved values at all and keeps
 * the defaults. */
void migrate_settings(obs_data_t *settings);

/* A new reference: the settings as the renderer should read them. */
obs_data_t *resolve_settings(obs_data_t *settings);

/* An element as it is drawn, for the layout editor. */
struct ElementBox {
	std::string prefix; /* "art_", "t0_" ... "t5_", "bar_" */
	std::string label;
	Box box;
	bool enabled = false;
	int z = 0;
};

std::vector<ElementBox> element_boxes(obs_data_t *settings);

/* Switches to Free placement, writing every element's current position and
 * size into its own fields first so that nothing moves. */
void detach_to_free_placement(obs_data_t *settings);

/* Moves or resizes one element under Free placement. `box` is in widget pixels
 * from the top left; it is stored relative to the element's own anchor. */
void place_element(obs_data_t *settings, const std::string &prefix, const Box &box);

} // namespace vr::widget
