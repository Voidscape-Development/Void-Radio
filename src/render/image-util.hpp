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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/*
 * Image decoding and analysis for the Music Widget, kept behind a plain
 * interface so that Qt types stay out of every other header. Decoding happens
 * on a worker thread; only the resulting byte buffer crosses into the graphics
 * thread, where it becomes a texture.
 */

namespace vr {

/* Straight (non premultiplied) RGBA, one byte per channel, tightly packed.
 * That is the layout GS_RGBA expects. */
struct RgbaImage {
	std::vector<uint8_t> pixels;
	uint32_t width = 0;
	uint32_t height = 0;

	bool valid() const { return width > 0 && height > 0 && pixels.size() == (size_t)width * height * 4; }

	void clear()
	{
		pixels.clear();
		width = 0;
		height = 0;
	}
};

/* Decodes an encoded image (JPEG, PNG, and whatever else the Qt image plugins
 * handle). When `max_dimension` is non zero the result is scaled down so that
 * neither side exceeds it, preserving aspect ratio. */
bool decode_image(const uint8_t *data, size_t size, RgbaImage &out, uint32_t max_dimension = 0);
bool decode_image_file(const std::string &path, RgbaImage &out, uint32_t max_dimension = 0);

/*
 * Produces a blurred, optionally darkened and desaturated copy, sized to
 * `width` x `height` and cropped to fill it. `strength` runs 0..1 and picks how
 * far the image is knocked down before being scaled back up, which is what
 * actually does the blurring; `darken` and `desaturate` also run 0..1.
 */
bool blur_image(const RgbaImage &source, uint32_t width, uint32_t height, float strength, float darken,
		float desaturate, RgbaImage &out);

/*
 * Picks a representative accent colour: the most prominent reasonably
 * saturated hue in the image, nudged into a range that still reads against
 * both dark and light backgrounds. Returns the colour in the same 0xAABBGGRR
 * packing OBS colour settings use, so it can be handed straight to
 * vec4_from_rgba. Returns 0 when the image is empty.
 */
uint32_t dominant_color(const RgbaImage &source);

} // namespace vr
