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

#include "ui/editor-snap.hpp"

#include <cmath>

namespace vr::editor {

namespace {

/* The closest target to any of `points` within `threshold`, as the distance to
 * move and the target hit. */
struct Match {
	bool found = false;
	float delta = 0.0f;
	float target = 0.0f;
};

Match closest(const std::vector<float> &points, const std::vector<float> &targets, float threshold)
{
	Match best;

	for (float point : points) {
		for (float target : targets) {
			const float delta = target - point;
			if (std::fabs(delta) > threshold)
				continue;
			if (!best.found || std::fabs(delta) < std::fabs(best.delta)) {
				best.found = true;
				best.delta = delta;
				best.target = target;
			}
		}
	}

	return best;
}

} // namespace

SnapResult snap(const Rect &rect, unsigned edges, float canvas_width, float canvas_height,
		const std::vector<Rect> &others, float threshold, float min_size)
{
	SnapResult result;
	result.rect = rect;

	std::vector<float> xs = {0.0f, canvas_width * 0.5f, canvas_width};
	std::vector<float> ys = {0.0f, canvas_height * 0.5f, canvas_height};

	for (const Rect &other : others) {
		xs.push_back(other.x);
		xs.push_back(other.x + other.width * 0.5f);
		xs.push_back(other.x + other.width);
		ys.push_back(other.y);
		ys.push_back(other.y + other.height * 0.5f);
		ys.push_back(other.y + other.height);
	}

	Rect &r = result.rect;
	const bool move = edges == EdgeAll;

	/* Horizontal. A move lines up either edge or the centre; a resize only
	 * the edge being dragged. */
	if (move) {
		const Match m = closest({r.x, r.x + r.width * 0.5f, r.x + r.width}, xs, threshold);
		if (m.found) {
			r.x += m.delta;
			result.guides.push_back({true, m.target});
		}
	} else if (edges & EdgeLeft) {
		const Match m = closest({r.x}, xs, threshold);
		if (m.found && r.width - m.delta >= min_size) {
			r.x += m.delta;
			r.width -= m.delta;
			result.guides.push_back({true, m.target});
		}
	} else if (edges & EdgeRight) {
		const Match m = closest({r.x + r.width}, xs, threshold);
		if (m.found && r.width + m.delta >= min_size) {
			r.width += m.delta;
			result.guides.push_back({true, m.target});
		}
	}

	/* Vertical, the same way. */
	if (move) {
		const Match m = closest({r.y, r.y + r.height * 0.5f, r.y + r.height}, ys, threshold);
		if (m.found) {
			r.y += m.delta;
			result.guides.push_back({false, m.target});
		}
	} else if (edges & EdgeTop) {
		const Match m = closest({r.y}, ys, threshold);
		if (m.found && r.height - m.delta >= min_size) {
			r.y += m.delta;
			r.height -= m.delta;
			result.guides.push_back({false, m.target});
		}
	} else if (edges & EdgeBottom) {
		const Match m = closest({r.y + r.height}, ys, threshold);
		if (m.found && r.height + m.delta >= min_size) {
			r.height += m.delta;
			result.guides.push_back({false, m.target});
		}
	}

	return result;
}

} // namespace vr::editor
