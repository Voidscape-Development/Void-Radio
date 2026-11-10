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

#include <algorithm>
#include <cmath>
#include <numeric>

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

Rect snap_to_grid(const Rect &rect, unsigned edges, float size, float min_size)
{
	if (size < 1.0f)
		return rect;

	const auto round_to = [size](float v) {
		return std::round(v / size) * size;
	};

	Rect r = rect;

	if (edges == EdgeAll) {
		r.x = round_to(r.x);
		r.y = round_to(r.y);
		return r;
	}

	float left = r.x, right = r.x + r.width, top = r.y, bottom = r.y + r.height;

	if (edges & EdgeLeft)
		left = std::min(round_to(left), right - min_size);
	if (edges & EdgeRight)
		right = std::max(round_to(right), left + min_size);
	if (edges & EdgeTop)
		top = std::min(round_to(top), bottom - min_size);
	if (edges & EdgeBottom)
		bottom = std::max(round_to(bottom), top + min_size);

	return Rect{left, top, right - left, bottom - top};
}

Rect bounds_of(const std::vector<Rect> &rects)
{
	if (rects.empty())
		return Rect{};

	float left = rects[0].x, top = rects[0].y;
	float right = rects[0].x + rects[0].width, bottom = rects[0].y + rects[0].height;

	for (const Rect &r : rects) {
		left = std::min(left, r.x);
		top = std::min(top, r.y);
		right = std::max(right, r.x + r.width);
		bottom = std::max(bottom, r.y + r.height);
	}

	return Rect{left, top, right - left, bottom - top};
}

std::vector<Rect> align_rects(const std::vector<Rect> &rects, Align how, const Rect &target)
{
	std::vector<Rect> out = rects;

	for (Rect &r : out) {
		switch (how) {
		case Align::Left:
			r.x = target.x;
			break;
		case Align::HorizontalCenter:
			r.x = target.x + (target.width - r.width) * 0.5f;
			break;
		case Align::Right:
			r.x = target.x + target.width - r.width;
			break;
		case Align::Top:
			r.y = target.y;
			break;
		case Align::VerticalCenter:
			r.y = target.y + (target.height - r.height) * 0.5f;
			break;
		case Align::Bottom:
			r.y = target.y + target.height - r.height;
			break;
		}

		r.x = std::round(r.x);
		r.y = std::round(r.y);
	}

	return out;
}

std::vector<Rect> distribute_rects(const std::vector<Rect> &rects, bool horizontal)
{
	std::vector<Rect> out = rects;
	if (out.size() < 3)
		return out;

	std::vector<size_t> order(out.size());
	std::iota(order.begin(), order.end(), 0);

	const auto start = [horizontal](const Rect &r) {
		return horizontal ? r.x : r.y;
	};
	const auto length = [horizontal](const Rect &r) {
		return horizontal ? r.width : r.height;
	};

	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return start(out[a]) < start(out[b]); });

	const Rect &first = out[order.front()];
	const Rect &last = out[order.back()];
	const float span = start(last) + length(last) - start(first);

	float total = 0.0f;
	for (const Rect &r : out)
		total += length(r);

	const float gap = (span - total) / (float)(out.size() - 1);

	float at = start(first) + length(first) + gap;
	for (size_t i = 1; i + 1 < order.size(); i++) {
		Rect &r = out[order[i]];
		if (horizontal)
			r.x = std::round(at);
		else
			r.y = std::round(at);
		at += length(r) + gap;
	}

	return out;
}

} // namespace vr::editor
