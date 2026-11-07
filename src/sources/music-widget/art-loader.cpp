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

namespace vr::widget {

namespace {

/* Cover art is never shown large enough to justify keeping the original
 * around, and the smaller copy makes the accent colour pass cheap. */
constexpr uint32_t MAX_ART_DIMENSION = 1024;

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

	const std::string key = request.path + '\n' + request.placeholder;

	if (key != decoded_key_) {
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
