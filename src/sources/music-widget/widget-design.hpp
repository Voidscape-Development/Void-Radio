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
	Neon = 6,
	Cassette = 7,
	Ticker = 8,
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
	/* "bg_", "art_", "t0_" ... "t5_", "bar_", and the extras "viz_",
	 * "img_", "icon_", "glow_". */
	std::string prefix;
	std::string label;
	Box box;
	bool enabled = false;
	int z = 0;

	/* Placed by hand under every layout: the extras are never arranged by
	 * one, so moving them does not switch the widget to free placement. */
	bool always_free = false;

	/* The panel fills the widget and is not moved on the canvas. */
	bool movable = true;

	/* An editor setting only: a locked element is not dragged. */
	bool locked = false;
};

/* Every element, panel first, in the order they are listed. */
std::vector<ElementBox> element_boxes(obs_data_t *settings);

/* True for the extras, which keep their own position under any layout. */
bool is_always_free(const std::string &prefix);

/* The z key an element's layer is stored under ("bg_z", "art_z" ...). */
std::string z_key(const std::string &prefix);

/*
 * Restacks the elements: `order` lists prefixes from the back to the front,
 * and each gets a layer number that keeps it there. Anything not listed keeps
 * its layer.
 */
void set_stacking(obs_data_t *settings, const std::vector<std::string> &order);

/* ------------------------------------------------------------------------- */
/* Designs                                                                    */
/* ------------------------------------------------------------------------- */

/*
 * A design is everything about how a widget looks, without which music source
 * it follows. Designs are what presets save, what export writes to a file and
 * what "copy design" hands to another widget.
 */

/* The widget's design as JSON, wrapped so a file can be recognised as one. */
std::string export_design(obs_data_t *settings);

/* Replaces everything but the music source binding with the design in `json`,
 * which may be wrapped by export_design or a bare settings object. Returns
 * false, leaving `settings` alone, when the JSON cannot be read. */
bool apply_design(obs_data_t *settings, const char *json);

struct BuiltinDesign {
	const char *name; /* a locale key */
	std::string json;
};

/* The designs that ship with the plugin. */
std::vector<BuiltinDesign> builtin_designs();

/* Switches to Free placement, writing every element's current position and
 * size into its own fields first so that nothing moves. */
void detach_to_free_placement(obs_data_t *settings);

/* Moves or resizes one element under Free placement. `box` is in widget pixels
 * from the top left; it is stored relative to the element's own anchor. */
void place_element(obs_data_t *settings, const std::string &prefix, const Box &box);

} // namespace vr::widget
