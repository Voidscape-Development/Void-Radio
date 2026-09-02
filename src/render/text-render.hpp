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

#include "render/image-util.hpp"

#include <cstdint>
#include <string>

/*
 * Text rasterisation for the Music Widget.
 *
 * OBS has no text drawing of its own, so the widget renders its own glyphs
 * through Qt and uploads the result as a texture. Everything decorative
 * (gradient fill, outline, shadow, underline) happens here at raster time
 * rather than in a shader, which keeps the draw side to a single textured
 * quad per text element.
 *
 * Rasterising is not cheap, so callers are expected to hold on to the result
 * and only redo it when the string or the style actually changes. Marquee
 * scrolling in particular is a draw time UV offset over a texture that is
 * rasterised once.
 */

namespace vr {

enum class TextTransform {
	None = 0,
	Upper = 1,
	Lower = 2,
	Title = 3,
};

enum class TextAlign {
	Left = 0,
	Center = 1,
	Right = 2,
};

enum class TextVAlign {
	Top = 0,
	Middle = 1,
	Bottom = 2,
};

enum class TextOverflow {
	None = 0,     /* draw at natural size, spilling past the box */
	Marquee = 1,  /* scroll horizontally */
	Shrink = 2,   /* reduce the point size until it fits */
	Ellipsis = 3, /* truncate with an ellipsis */
	Wrap = 4,     /* word wrap onto further lines */
};

enum class MarqueeStyle {
	Loop = 0,   /* travels in one direction, repeating after a gap */
	Bounce = 1, /* runs to the end, pauses, and comes back */
};

enum class EllipsisSide {
	Right = 0,
	Left = 1,
	Middle = 2,
};

struct TextStyle {
	std::string font_family = "Arial";
	std::string font_style; /* the face's own style name, when it has one */
	int font_size = 32;
	bool bold = false;
	bool italic = false;
	bool underline = false;

	/* Colours are packed the way OBS colour settings are: 0xAABBGGRR. */
	uint32_t color = 0xFFFFFFFF;
	uint32_t color2 = 0xFFFFFFFF;
	bool gradient = false;
	float gradient_angle = 90.0f;

	float outline_width = 0.0f;
	uint32_t outline_color = 0xFF000000;

	bool shadow = false;
	float shadow_x = 2.0f;
	float shadow_y = 2.0f;
	float shadow_blur = 4.0f;
	uint32_t shadow_color = 0xC0000000;

	float letter_spacing = 0.0f; /* pixels added between glyphs */
	float line_height = 1.0f;    /* multiplier on the font's own line height */

	TextTransform transform = TextTransform::None;
	TextAlign align = TextAlign::Left;
	TextVAlign valign = TextVAlign::Top;

	TextOverflow overflow = TextOverflow::None;
	MarqueeStyle marquee_style = MarqueeStyle::Loop;
	EllipsisSide ellipsis_side = EllipsisSide::Right;

	int max_lines = 2;
	float shrink_min = 50.0f;    /* percent of the nominal size */
	float marquee_gap = 64.0f;   /* pixels between repeats in loop mode */
	float marquee_speed = 60.0f; /* pixels per second */
	float marquee_pause = 1.0f;  /* seconds held at each end in bounce mode */

	bool operator==(const TextStyle &other) const;
	bool operator!=(const TextStyle &other) const { return !(*this == other); }
};

struct RasterizedText {
	RgbaImage image;

	/* The texture is drawn at (element x - pad, element y - pad) so that
	 * outlines and shadows have room to spill without being cut off. */
	int pad = 0;

	/* Size of the whole raster, kept separately from `image` because the
	 * caller is expected to free the pixel buffer once it has been uploaded
	 * and the marquee still has to know how wide a scroll period is. */
	uint32_t texture_width = 0;
	uint32_t texture_height = 0;

	/* Width of a single copy of the text, ignoring the marquee gap. */
	uint32_t content_width = 0;
	uint32_t content_height = 0;

	/* True when the text did not fit and the caller should scroll it. In
	 * that case the texture holds one copy plus the gap, and its full width
	 * is the scroll period. */
	bool scrolls = false;

	bool valid() const { return image.valid(); }
};

/*
 * Renders `utf8` into `out`. `box_width` and `box_height` are the element's own
 * size and decide wrapping, eliding, shrinking and alignment.
 *
 * Returns false for empty text or an unusable size, leaving `out` cleared.
 * Must be called from a thread that is allowed to use Qt's raster paint engine;
 * the graphics thread qualifies, the audio thread does not.
 */
bool rasterize_text(const std::string &utf8, const TextStyle &style, uint32_t box_width, uint32_t box_height,
		    RasterizedText &out);

/*
 * Where a marquee should be scrolled to at time `elapsed`, in pixels, given the
 * rasterised text. Returns 0 when the text is not scrolling.
 */
float marquee_offset(const RasterizedText &text, const TextStyle &style, uint32_t box_width, float elapsed);

} // namespace vr
