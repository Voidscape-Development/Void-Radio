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

/* The smaller extra elements: a picture of the user's own, a play state icon,
 * and the cover blurred inside a box as a glow. */

#include "sources/music-widget/widget.hpp"

#include <plugin-support.h>

#include <algorithm>
#include <cmath>

namespace vr::widget {

namespace {

void read_box(Geometry &geometry, obs_data_t *settings, const std::string &prefix)
{
	geometry.enabled = obs_data_get_bool(settings, key_for(prefix, "enable").c_str());
	geometry.anchor = (Anchor)obs_data_get_int(settings, key_for(prefix, "anchor").c_str());
	geometry.x = (float)obs_data_get_double(settings, key_for(prefix, "x").c_str());
	geometry.y = (float)obs_data_get_double(settings, key_for(prefix, "y").c_str());
	geometry.width = (float)std::max<int64_t>(1, obs_data_get_int(settings, key_for(prefix, "width").c_str()));
	geometry.height = (float)std::max<int64_t>(1, obs_data_get_int(settings, key_for(prefix, "height").c_str()));
	geometry.opacity = (float)obs_data_get_double(settings, key_for(prefix, "opacity").c_str()) / 100.0f;
	geometry.z = (int)obs_data_get_int(settings, key_for(prefix, "z").c_str());
}

void load_image(ImageElement &image, const char *path)
{
	const std::string next = path ? path : "";

	if (image.image_valid && next == image.path)
		return;

	if (image.image_valid) {
		obs_enter_graphics();
		vr_image_file_free(&image.image);
		obs_leave_graphics();
		image.image_valid = false;
	}

	image.path = next;
	if (image.path.empty())
		return;

	vr_image_file_init(&image.image, image.path.c_str(), GS_IMAGE_ALPHA_STRAIGHT);

	obs_enter_graphics();
	vr_image_file_init_texture(&image.image);
	obs_leave_graphics();

	image.image_valid = vr_image_file_base(image.image).loaded;

	if (!image.image_valid)
		obs_log(LOG_WARNING, "music widget could not load image '%s'", image.path.c_str());
}

/* The three icons as white masks, antialiased by sampling each pixel 4x4. */
enum class Glyph { Play, Pause, Stop };

bool inside(Glyph glyph, float u, float v)
{
	switch (glyph) {
	case Glyph::Play: {
		/* A triangle pointing right, nudged right so it looks centred. */
		const float left = 0.24f, right = 0.86f, top = 0.14f, bottom = 0.86f;
		if (u < left || u > right)
			return false;
		const float t = (u - left) / (right - left);
		const float half = (bottom - top) * 0.5f * (1.0f - t);
		return std::fabs(v - 0.5f) <= half;
	}
	case Glyph::Pause:
		return v >= 0.16f && v <= 0.84f && ((u >= 0.22f && u <= 0.42f) || (u >= 0.58f && u <= 0.78f));
	case Glyph::Stop:
	default:
		return u >= 0.2f && u <= 0.8f && v >= 0.2f && v <= 0.8f;
	}
}

gs_texture_t *make_glyph(Glyph glyph, uint32_t size)
{
	RgbaImage image;
	image.width = size;
	image.height = size;
	image.pixels.assign((size_t)size * size * 4, 255);

	constexpr int SAMPLES = 4;

	for (uint32_t y = 0; y < size; y++) {
		for (uint32_t x = 0; x < size; x++) {
			int hits = 0;
			for (int sy = 0; sy < SAMPLES; sy++)
				for (int sx = 0; sx < SAMPLES; sx++) {
					const float u = ((float)x + ((float)sx + 0.5f) / SAMPLES) / (float)size;
					const float v = ((float)y + ((float)sy + 0.5f) / SAMPLES) / (float)size;
					hits += inside(glyph, u, v) ? 1 : 0;
				}
			image.pixels[((size_t)y * size + x) * 4 + 3] = (uint8_t)(hits * 255 / (SAMPLES * SAMPLES));
		}
	}

	return create_texture(image);
}

} // namespace

void read_extras(MusicWidget *widget, obs_data_t *settings)
{
	ImageElement &image = widget->image;
	read_box(image.geometry, settings, "img_");
	image.fit = (ArtFit)obs_data_get_int(settings, "img_fit");
	image.corner_radius = (float)obs_data_get_double(settings, "img_corner_radius");
	if (image.geometry.enabled)
		load_image(image, obs_data_get_string(settings, "img_path"));

	IconElement &icon = widget->icon;
	read_box(icon.geometry, settings, "icon_");
	icon.color = (uint32_t)obs_data_get_int(settings, "icon_color");
	icon.follow_accent = obs_data_get_bool(settings, "icon_accent");
	icon.tone = (AccentTone)obs_data_get_int(settings, "icon_tone");
	icon.show = (IconShow)obs_data_get_int(settings, "icon_show");

	GlowElement &glow = widget->glow;
	read_box(glow.geometry, settings, "glow_");
	glow.corner_radius = (float)obs_data_get_double(settings, "glow_corner_radius");
	glow.softness = (float)obs_data_get_double(settings, "glow_softness");
}

void tick_extras(MusicWidget *widget)
{
	IconElement &icon = widget->icon;

	const uint32_t size =
		icon.geometry.enabled
			? (uint32_t)std::clamp(std::lround(std::min(icon.geometry.width, icon.geometry.height)), 4L,
					       512L)
			: 0;

	if (size == icon.texture_size)
		return;

	destroy_texture(icon.play);
	destroy_texture(icon.pause);
	destroy_texture(icon.stop);
	icon.texture_size = size;

	if (!size)
		return;

	icon.play = make_glyph(Glyph::Play, size);
	icon.pause = make_glyph(Glyph::Pause, size);
	icon.stop = make_glyph(Glyph::Stop, size);
}

void draw_image(MusicWidget *widget)
{
	ImageElement &image = widget->image;
	if (!image.geometry.enabled || !image.image_valid)
		return;

	draw_texture_box(widget, vr_image_file_base(image.image).texture, image.geometry, image.fit,
			 image.corner_radius, 0.0f, 0xFFFFFFFF);
}

void draw_icon(MusicWidget *widget, PlayState state)
{
	IconElement &icon = widget->icon;
	if (!icon.geometry.enabled)
		return;

	if (icon.show == IconShow::NotPlaying && state == PlayState::Playing)
		return;

	gs_texture_t *texture = state == PlayState::Playing  ? icon.play
				: state == PlayState::Paused ? icon.pause
							     : icon.stop;
	if (!texture)
		return;

	const uint32_t tint = resolve_color(widget, icon.color, icon.follow_accent, icon.tone);
	draw_texture_box(widget, texture, icon.geometry, ArtFit::Contain, 0.0f, 0.0f, tint);
}

void draw_glow(MusicWidget *widget)
{
	GlowElement &glow = widget->glow;
	if (!glow.geometry.enabled || !widget->glow_texture)
		return;

	draw_texture_box(widget, widget->glow_texture, glow.geometry, ArtFit::Cover, glow.corner_radius, glow.softness,
			 0xFFFFFFFF);
}

void destroy_extras(MusicWidget *widget)
{
	ImageElement &image = widget->image;
	if (image.image_valid) {
		vr_image_file_free(&image.image);
		image.image_valid = false;
	}

	IconElement &icon = widget->icon;
	for (gs_texture_t **texture : {&icon.play, &icon.pause, &icon.stop}) {
		if (*texture)
			gs_texture_destroy(*texture);
		*texture = nullptr;
	}
	icon.texture_size = 0;
}

} // namespace vr::widget
