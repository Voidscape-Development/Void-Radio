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

#include "sources/music-widget/art-loader.hpp"
#include "music/album-art.hpp"

#include <algorithm>
#include <cmath>

namespace vr::widget {

namespace {

/* Cover art is never shown large enough to justify keeping the original
 * around, and the smaller copy makes the accent colour pass cheap. */
constexpr uint32_t MAX_ART_DIMENSION = 1024;

/* A cover for the sample track: a diagonal gradient with a soft sun, in one of
 * two colour schemes so the two sample tracks look different. */
RgbaImage sample_cover(int variant)
{
	constexpr uint32_t SIZE = 384;

	struct Rgb {
		float r, g, b;
	};
	const Rgb a = variant % 2 ? Rgb{0.95f, 0.30f, 0.55f} : Rgb{0.10f, 0.75f, 0.80f};
	const Rgb b = variant % 2 ? Rgb{0.25f, 0.10f, 0.55f} : Rgb{0.05f, 0.15f, 0.45f};
	const Rgb sun = variant % 2 ? Rgb{1.0f, 0.80f, 0.35f} : Rgb{0.85f, 1.0f, 0.95f};

	RgbaImage image;
	image.width = SIZE;
	image.height = SIZE;
	image.pixels.resize((size_t)SIZE * SIZE * 4);

	for (uint32_t y = 0; y < SIZE; y++) {
		for (uint32_t x = 0; x < SIZE; x++) {
			const float u = (float)x / (SIZE - 1);
			const float v = (float)y / (SIZE - 1);
			const float t = (u + v) * 0.5f;

			float r = a.r + (b.r - a.r) * t;
			float g = a.g + (b.g - a.g) * t;
			float bl = a.b + (b.b - a.b) * t;

			const float dx = u - 0.62f;
			const float dy = v - 0.38f;
			const float d = dx * dx + dy * dy;
			const float glow = d < 0.045f ? 1.0f : std::max(0.0f, 1.0f - (d - 0.045f) * 14.0f) * 0.5f;

			/* Horizontal stripes across the lower half, for some texture. */
			const float stripe = (v > 0.6f && ((int)(v * 40.0f) % 3 == 0)) ? 0.12f : 0.0f;

			r = r + (sun.r - r) * glow - stripe;
			g = g + (sun.g - g) * glow - stripe;
			bl = bl + (sun.b - bl) * glow - stripe;

			uint8_t *pixel = &image.pixels[((size_t)y * SIZE + x) * 4];
			pixel[0] = (uint8_t)std::lround(std::clamp(r, 0.0f, 1.0f) * 255.0f);
			pixel[1] = (uint8_t)std::lround(std::clamp(g, 0.0f, 1.0f) * 255.0f);
			pixel[2] = (uint8_t)std::lround(std::clamp(bl, 0.0f, 1.0f) * 255.0f);
			pixel[3] = 255;
		}
	}

	return image;
}

} // namespace

void ArtLoader::run()
{
	std::unique_lock<std::mutex> lock(mutex_);

	while (!quit_) {
		condition_.wait(lock, [this]() { return quit_ || have_pending_; });

		if (quit_)
			break;

		const Request request = pending_;
		have_pending_ = false;

		lock.unlock();
		Result result = process(request);
		lock.lock();

		/* A request that arrived while this one was running
		 * wins, so a stale result is never published. */
		if (!have_pending_) {
			result_ = std::move(result);
			have_result_ = true;
		}
	}
}

ArtLoader::Result ArtLoader::process(const Request &request)
{
	Result result;

	const std::string key = request.path + '\n' + request.placeholder + '\n' + std::to_string(request.sample);

	if (key != decoded_key_ && request.sample) {
		decoded_key_ = key;
		decoded_ = sample_cover(request.sample);
		decoded_accent_ = dominant_color(decoded_);
		decoded_accent_valid_ = decoded_accent_ != 0;
	} else if (key != decoded_key_) {
		decoded_key_ = key;
		decoded_.clear();

		AlbumArtData embedded;

		if (read_embedded_art(request.path, embedded))
			decode_image(embedded.bytes.data(), embedded.bytes.size(), decoded_, MAX_ART_DIMENSION);

		if (!decoded_.valid()) {
			const std::string sidecar = find_sidecar_art(request.path);
			if (!sidecar.empty())
				decode_image_file(sidecar, decoded_, MAX_ART_DIMENSION);
		}

		if (!decoded_.valid() && !request.placeholder.empty())
			decode_image_file(request.placeholder, decoded_, MAX_ART_DIMENSION);

		decoded_accent_valid_ = false;

		if (decoded_.valid()) {
			decoded_accent_ = dominant_color(decoded_);
			decoded_accent_valid_ = decoded_accent_ != 0;
		}
	}

	if (!decoded_.valid())
		return result;

	result.art = decoded_;
	result.have_art = true;
	result.accent = decoded_accent_;
	result.accent_valid = decoded_accent_valid_;

	if (request.want_blur && request.blur_width > 0 && request.blur_height > 0) {
		result.have_blur = blur_image(decoded_, request.blur_width, request.blur_height, request.blur_strength,
					      request.blur_darken, request.blur_desaturate, result.blur);
	}

	return result;
}

} // namespace vr::widget
