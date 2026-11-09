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

#include "sources/music-widget/widget-palette.hpp"

#include <algorithm>
#include <cmath>

namespace vr::widget {

namespace {

struct Hsl {
	float h = 0.0f; /* 0..360 */
	float s = 0.0f;
	float l = 0.0f;
};

float channel(uint32_t color, int shift)
{
	return (float)((color >> shift) & 0xFF) / 255.0f;
}

uint32_t pack(float r, float g, float b, uint32_t alpha_from)
{
	const auto byte = [](float v) {
		return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
	};
	return (alpha_from & 0xFF000000u) | (byte(b) << 16) | (byte(g) << 8) | byte(r);
}

Hsl to_hsl(uint32_t color)
{
	const float r = channel(color, 0);
	const float g = channel(color, 8);
	const float b = channel(color, 16);

	const float max = std::max({r, g, b});
	const float min = std::min({r, g, b});
	const float delta = max - min;

	Hsl out;
	out.l = (max + min) * 0.5f;

	if (delta <= 0.0f)
		return out;

	out.s = delta / (1.0f - std::fabs(2.0f * out.l - 1.0f));

	if (max == r)
		out.h = 60.0f * std::fmod((g - b) / delta, 6.0f);
	else if (max == g)
		out.h = 60.0f * ((b - r) / delta + 2.0f);
	else
		out.h = 60.0f * ((r - g) / delta + 4.0f);

	if (out.h < 0.0f)
		out.h += 360.0f;

	return out;
}

uint32_t from_hsl(const Hsl &hsl, uint32_t alpha_from)
{
	const float c = (1.0f - std::fabs(2.0f * hsl.l - 1.0f)) * hsl.s;
	const float hp = hsl.h / 60.0f;
	const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));

	float r = 0.0f, g = 0.0f, b = 0.0f;

	if (hp < 1.0f) {
		r = c;
		g = x;
	} else if (hp < 2.0f) {
		r = x;
		g = c;
	} else if (hp < 3.0f) {
		g = c;
		b = x;
	} else if (hp < 4.0f) {
		g = x;
		b = c;
	} else if (hp < 5.0f) {
		r = x;
		b = c;
	} else {
		r = c;
		b = x;
	}

	const float m = hsl.l - c * 0.5f;
	return pack(r + m, g + m, b + m, alpha_from);
}

float linearise(float v)
{
	return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

constexpr uint32_t NEAR_BLACK = 0xFF141414;
constexpr uint32_t WHITE = 0xFFFFFFFF;

} // namespace

float relative_luminance(uint32_t color)
{
	return 0.2126f * linearise(channel(color, 0)) + 0.7152f * linearise(channel(color, 8)) +
	       0.0722f * linearise(channel(color, 16));
}

float contrast_ratio(uint32_t a, uint32_t b)
{
	const float la = relative_luminance(a);
	const float lb = relative_luminance(b);
	return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

Palette derive_palette(uint32_t accent)
{
	Palette palette;
	palette.accent = accent;

	const Hsl base = to_hsl(accent);

	/* Dark enough to sit behind white text, light enough to sit behind
	 * dark text, keeping the hue and most of the saturation either way. */
	Hsl dark = base;
	dark.l = std::min(base.l * 0.45f, 0.22f);
	dark.s = std::min(base.s, 0.85f);
	palette.dark = from_hsl(dark, accent);

	/* Some hues are much brighter than others at the same lightness (a
	 * dark yellow is lighter than a dark blue), so each tone is pushed on
	 * until it actually reads against white or black text. */
	while (contrast_ratio(palette.dark, WHITE) < 7.0f && dark.l > 0.02f) {
		dark.l -= 0.02f;
		palette.dark = from_hsl(dark, accent);
	}

	Hsl light = base;
	light.l = std::max(base.l + (1.0f - base.l) * 0.55f, 0.78f);
	light.s = std::min(base.s, 0.9f);
	palette.light = from_hsl(light, accent);

	while (contrast_ratio(palette.light, NEAR_BLACK) < 7.0f && light.l < 0.98f) {
		light.l += 0.02f;
		palette.light = from_hsl(light, accent);
	}

	palette.contrast = contrast_ratio(accent, NEAR_BLACK) >= contrast_ratio(accent, WHITE) ? NEAR_BLACK : WHITE;
	palette.contrast = (palette.contrast & 0x00FFFFFFu) | (accent & 0xFF000000u);

	return palette;
}

uint32_t palette_tone(const Palette &palette, AccentTone tone)
{
	switch (tone) {
	case AccentTone::Dark:
		return palette.dark;
	case AccentTone::Light:
		return palette.light;
	case AccentTone::Contrast:
		return palette.contrast;
	default:
		return palette.accent;
	}
}

} // namespace vr::widget
