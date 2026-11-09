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

#include <cstdint>

namespace vr::widget {

/*
 * The album colour, and the tones worked out from it.
 *
 * One colour is sampled from the cover. Elements that follow it can ask for
 * that colour as it is, a darker or a lighter tone of it, or whichever of
 * near-black and white reads best on top of it, so a card can have a panel in
 * the dark tone, a bar in the accent and text that stays legible whatever the
 * cover. Pure arithmetic, tested on its own.
 *
 * Colours are 0xAABBGGRR, as OBS stores them. Alpha is passed through.
 */

/* Stored in settings; the numbers must not change. */
enum class AccentTone {
	Accent = 0,
	Dark = 1,
	Light = 2,
	Contrast = 3,
};

struct Palette {
	uint32_t accent = 0xFFFFFFFF;
	uint32_t dark = 0xFF000000;
	uint32_t light = 0xFFFFFFFF;
	uint32_t contrast = 0xFF000000;
};

Palette derive_palette(uint32_t accent);

uint32_t palette_tone(const Palette &palette, AccentTone tone);

/* Relative luminance, 0..1, as WCAG defines it. */
float relative_luminance(uint32_t color);

/* WCAG contrast ratio between two colours, 1..21. */
float contrast_ratio(uint32_t a, uint32_t b);

} // namespace vr::widget
