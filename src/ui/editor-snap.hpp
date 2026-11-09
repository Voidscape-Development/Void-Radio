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

namespace vr::editor {

/*
 * Snapping for the layout editor: pulls an element being dragged onto the
 * widget's edges and centre lines, and onto the edges and centres of the other
 * elements, and reports the guide lines to draw. Plain arithmetic so it can be
 * tested without a window.
 */

struct Rect {
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

/* Which edges a drag moves: all four for a move, one or two for a resize. */
enum Edge : unsigned {
	EdgeLeft = 1,
	EdgeRight = 2,
	EdgeTop = 4,
	EdgeBottom = 8,
	EdgeAll = 15,
};

struct Guide {
	bool vertical = true; /* a vertical line at x = position, or horizontal at y */
	float position = 0.0f;
};

struct SnapResult {
	Rect rect;
	std::vector<Guide> guides;
};

/* `threshold` is in widget pixels. Sizes never snap below `min_size`. */
SnapResult snap(const Rect &rect, unsigned edges, float canvas_width, float canvas_height,
		const std::vector<Rect> &others, float threshold, float min_size = 4.0f);

/* Rounds the edges a drag moves onto a grid of `size` pixels. A move keeps the
 * size and lands the top left corner on the grid. */
Rect snap_to_grid(const Rect &rect, unsigned edges, float size, float min_size = 4.0f);

/* The smallest rectangle holding all of `rects`. */
Rect bounds_of(const std::vector<Rect> &rects);

enum class Align {
	Left,
	HorizontalCenter,
	Right,
	Top,
	VerticalCenter,
	Bottom,
};

/* Lines up `rects` along one edge or centre line of `target`: the selection's
 * own bounds when several are lined up, or the widget when it is one. */
std::vector<Rect> align_rects(const std::vector<Rect> &rects, Align how, const Rect &target);

/* Spaces `rects` out evenly between the outermost two, left to right or top to
 * bottom, leaving equal gaps. The outermost two stay where they are. */
std::vector<Rect> distribute_rects(const std::vector<Rect> &rects, bool horizontal);

} // namespace vr::editor
