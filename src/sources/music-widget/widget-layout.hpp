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

#include <vector>

namespace vr::widget {

/*
 * The Music Widget's automatic layouts.
 *
 * Given the widget's size and which lines are switched on, this works out where
 * the art, each line of text and the progress bar go, and how big the type is.
 * Everything is proportional to the widget, so resizing it reflows the card
 * rather than leaving elements at stale pixel positions.
 *
 * Pure arithmetic with no OBS in it, so it is tested on its own and shared by
 * the widget and the layout editor.
 */

/* Stored in settings; the numbers must not change. */
enum class Arrangement {
	Free = 0, /* every element placed by hand, as before this existed */
	ArtLeft = 1,
	ArtRight = 2,
	Stacked = 3,
	Backdrop = 4,
	Compact = 5,
	TextOnly = 6,
};

enum class BarPlacement {
	UnderText = 0,
	BottomEdge = 1,
	Off = 2,
};

enum class LineRole {
	Title = 0,
	Subtitle = 1,
	Detail = 2,
	Time = 3,
};

/* How much bigger a line's box is than its type, to leave room for ascenders,
 * descenders and outlines. */
constexpr float LINE_BOX_RATIO = 1.32f;

float role_scale(LineRole role);
bool role_bold(LineRole role);

struct Box {
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
	bool visible = false;
};

struct LayoutLine {
	LineRole role = LineRole::Title;

	/* Set for a line that keeps its own font: its size is used as given
	 * instead of being worked out from the role. */
	float fixed_px = 0.0f;
};

struct LayoutRequest {
	Arrangement arrangement = Arrangement::ArtLeft;
	float width = 640.0f;
	float height = 180.0f;

	float spacing = 1.0f;    /* padding and gaps, 1 = normal */
	float art_scale = 1.0f;  /* art size, 1 = as large as the layout allows */
	float text_scale = 1.0f; /* type size, 1 = normal */
	float corner_radius = 0.0f;

	BarPlacement bar = BarPlacement::UnderText;

	/* The lines that are switched on, in order. */
	std::vector<LayoutLine> lines;
};

struct LayoutLineResult {
	Box box;
	float font_px = 0.0f;
	bool bold = false;
};

struct LayoutResult {
	Box art;
	Box bar;
	std::vector<LayoutLineResult> lines; /* one per requested line */

	bool center_text = false;
	bool art_as_backdrop = false; /* the art fills the card behind the text */
};

LayoutResult compute_layout(const LayoutRequest &request);

} // namespace vr::widget
