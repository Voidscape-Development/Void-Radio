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

#include "sources/music-widget/widget-layout.hpp"

#include <algorithm>
#include <cmath>

namespace vr::widget {

namespace {

/* Type is never laid out smaller or larger than this, whatever the widget's
 * size, before the text size setting is applied. */
constexpr float MIN_BASE_PX = 10.0f;
constexpr float MAX_BASE_PX = 96.0f;

/* Gap between lines, and above an under-text bar, as a share of the base type
 * size. */
constexpr float LINE_GAP = 0.08f;
constexpr float BAR_GAP = 0.32f;
constexpr float BAR_THICKNESS = 0.15f;

Box make_box(float x, float y, float width, float height)
{
	Box box;
	box.x = x;
	box.y = y;
	box.width = std::max(0.0f, width);
	box.height = std::max(0.0f, height);
	box.visible = box.width > 0.0f && box.height > 0.0f;
	return box;
}

/* Where the text sits, and how it lines up inside that space. */
struct TextRegion {
	Box area;
	float base_px = 0.0f;
	bool center = false;
	bool bottom = false;
};

} // namespace

float role_scale(LineRole role)
{
	switch (role) {
	case LineRole::Subtitle:
		return 0.74f;
	case LineRole::Detail:
		return 0.6f;
	case LineRole::Time:
		return 0.56f;
	default:
		return 1.0f;
	}
}

bool role_bold(LineRole role)
{
	return role == LineRole::Title;
}

LayoutResult compute_layout(const LayoutRequest &request)
{
	LayoutResult result;
	result.lines.resize(request.lines.size());

	const float W = std::max(1.0f, request.width);
	const float full_height = std::max(1.0f, request.height);

	/* A bar along the bottom edge gets its own strip, and everything else is
	 * laid out in the space above it. */
	const bool edge_bar = request.bar == BarPlacement::BottomEdge;
	const float edge_thickness = edge_bar ? std::max(3.0f, std::min(full_height * 0.04f, 10.0f)) : 0.0f;
	const float H = std::max(1.0f, full_height - edge_thickness);
	const float spacing = std::max(0.0f, request.spacing);
	const float art_scale = std::clamp(request.art_scale, 0.0f, 1.0f);
	const float min_side = std::min(W, H);

	TextRegion text;

	switch (request.arrangement) {
	case Arrangement::Stacked: {
		const float pad = std::min(W * 0.07f, H * 0.05f) * spacing;
		const float art = std::max(0.0f, std::min(W - 2.0f * pad, H * 0.58f)) * art_scale;
		const float gap = pad * 0.9f;

		result.art = make_box((W - art) * 0.5f, pad, art, art);

		const float top = result.art.visible ? pad + art + gap : pad;
		text.area = make_box(pad, top, W - 2.0f * pad, H - top - pad);
		text.base_px = W * 0.075f;
		text.center = true;
		break;
	}

	case Arrangement::Backdrop: {
		const float pad = min_side * 0.09f * spacing;

		result.art = make_box(0.0f, 0.0f, W, full_height);
		result.art.visible = false;
		result.art_as_backdrop = true;

		text.area = make_box(pad, pad, W - 2.0f * pad, H - 2.0f * pad);
		text.base_px = min_side * 0.12f;
		text.bottom = true;
		break;
	}

	case Arrangement::TextOnly: {
		const float pad = std::min(H * 0.16f, W * 0.06f) * spacing;
		text.area = make_box(pad, pad, W - 2.0f * pad, H - 2.0f * pad);
		text.base_px = H * 0.19f;
		break;
	}

	case Arrangement::ArtLeft:
	case Arrangement::ArtRight:
	case Arrangement::Compact:
	default: {
		const bool compact = request.arrangement == Arrangement::Compact;
		const float pad = std::min(H * (compact ? 0.14f : 0.11f), W * 0.05f) * spacing;
		const float art = std::max(0.0f, std::min(H - 2.0f * pad, W * 0.45f)) * art_scale;
		const float gap = art > 0.0f ? pad * 0.9f : 0.0f;
		const bool right = request.arrangement == Arrangement::ArtRight;

		result.art = make_box(right ? W - pad - art : pad, (H - art) * 0.5f, art, art);

		const float text_x = right ? pad : pad + art + gap;
		text.area = make_box(text_x, pad, W - 2.0f * pad - art - gap, H - 2.0f * pad);
		text.base_px = H * (compact ? 0.28f : 0.165f);
		break;
	}
	}

	result.center_text = text.center;

	float base = std::clamp(text.base_px, MIN_BASE_PX, MAX_BASE_PX) * std::max(0.1f, request.text_scale);

	/* Sizes for every line and the bar, then shrunk together if the stack
	 * would not fit in the space it has. */
	std::vector<float> px(request.lines.size());
	bool under_bar = request.bar == BarPlacement::UnderText;

	const auto bar_thickness = [&](float scale) {
		return std::max(3.0f, base * scale * BAR_THICKNESS);
	};

	const auto stack_height = [&](float scale) {
		float total = 0.0f;
		for (size_t i = 0; i < request.lines.size(); i++) {
			const LayoutLine &line = request.lines[i];
			const float size = line.fixed_px > 0.0f ? line.fixed_px : base * role_scale(line.role) * scale;
			px[i] = size;
			total += size * LINE_BOX_RATIO;
			if (i > 0)
				total += base * scale * LINE_GAP;
		}
		if (under_bar)
			total += base * scale * BAR_GAP + bar_thickness(scale);
		return total;
	};

	/* A bar that cannot fit at its thinnest is left out rather than drawn
	 * over the text. */
	if (under_bar && text.area.height < bar_thickness(0.0f) + 1.0f)
		under_bar = false;

	/* Shrinking is not quite proportional, because the bar has a minimum
	 * thickness, so it takes a few rounds to settle. */
	float scale = 1.0f;
	float total = stack_height(scale);

	for (int round = 0; round < 8 && total > text.area.height && total > 0.0f; round++) {
		scale *= text.area.height / total;
		total = stack_height(scale);
	}

	float y = text.area.y + (text.area.height - total) * (text.bottom ? 1.0f : 0.5f);

	for (size_t i = 0; i < request.lines.size(); i++) {
		if (i > 0)
			y += base * scale * LINE_GAP;

		const float height = px[i] * LINE_BOX_RATIO;
		LayoutLineResult &line = result.lines[i];
		line.box = make_box(text.area.x, y, text.area.width, height);
		line.font_px = px[i];
		line.bold = role_bold(request.lines[i].role);
		y += height;
	}

	if (under_bar) {
		const float thickness = bar_thickness(scale);
		y += base * scale * BAR_GAP;
		result.bar = make_box(text.area.x, y, text.area.width, thickness);
	} else if (edge_bar) {
		/* Kept clear of rounded corners so it does not poke out of them. */
		const float inset = std::min(std::max(0.0f, request.corner_radius) * 0.7f, W * 0.25f);
		result.bar = make_box(inset, full_height - edge_thickness, W - 2.0f * inset, edge_thickness);
	}

	return result;
}

} // namespace vr::widget
