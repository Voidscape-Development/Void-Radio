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

/* Layout maths and the drawing helpers every element uses. */

#include "sources/music-widget/widget.hpp"

#include <graphics/matrix4.h>

#include <cmath>

namespace vr::widget {

/* ------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* ------------------------------------------------------------------------- */

std::string key_for(const std::string &prefix, const char *suffix)
{
	return prefix + suffix;
}

std::string text_prefix(int slot)
{
	return "t" + std::to_string(slot) + "_";
}

static float anchor_fraction_x(Anchor anchor)
{
	switch (anchor) {
	case Anchor::TopCenter:
	case Anchor::Center:
	case Anchor::BottomCenter:
		return 0.5f;
	case Anchor::TopRight:
	case Anchor::MiddleRight:
	case Anchor::BottomRight:
		return 1.0f;
	default:
		return 0.0f;
	}
}

static float anchor_fraction_y(Anchor anchor)
{
	switch (anchor) {
	case Anchor::MiddleLeft:
	case Anchor::Center:
	case Anchor::MiddleRight:
		return 0.5f;
	case Anchor::BottomLeft:
	case Anchor::BottomCenter:
	case Anchor::BottomRight:
		return 1.0f;
	default:
		return 0.0f;
	}
}

/* Where the element's top left corner lands, given the canvas size. The
 * element is aligned to its anchor by the same fraction, so a right anchored
 * element grows leftwards and a centred one grows both ways. */
void resolve_position(const Geometry &geometry, uint32_t canvas_width, uint32_t canvas_height, float &x, float &y)
{
	const float fx = anchor_fraction_x(geometry.anchor);
	const float fy = anchor_fraction_y(geometry.anchor);

	x = (float)canvas_width * fx - geometry.width * fx + geometry.x;
	y = (float)canvas_height * fy - geometry.height * fy + geometry.y;
}

void draw_quad(gs_effect_t *effect, const DrawParams &params, bool sprite)
{
	if (!effect || params.width <= 0.0f || params.height <= 0.0f || params.opacity <= 0.0f)
		return;

	if (params.fill_mode == 2 && !params.texture)
		return;

	const bool linear_srgb = gs_get_linear_srgb();

	struct vec2 size;
	vec2_set(&size, params.width, params.height);

	struct vec4 color;
	struct vec4 color2;
	struct vec4 border;

	if (linear_srgb) {
		vec4_from_rgba_srgb(&color, params.color);
		vec4_from_rgba_srgb(&color2, params.color2);
		vec4_from_rgba_srgb(&border, params.border_color);
	} else {
		vec4_from_rgba(&color, params.color);
		vec4_from_rgba(&color2, params.color2);
		vec4_from_rgba(&border, params.border_color);
	}

	const float radians = params.gradient_angle * PI / 180.0f;
	struct vec2 gradient;
	vec2_set(&gradient, std::cos(radians), std::sin(radians));

	struct vec2 uv_scale;
	vec2_set(&uv_scale, params.uv_scale_x, params.uv_scale_y);

	struct vec2 uv_offset;
	vec2_set(&uv_offset, params.uv_offset_x, params.uv_offset_y);

	gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "draw_size"), &size);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "corner_radius"), params.corner_radius);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "border_width"), params.border_width);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "border_color"), &border);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "softness"), params.softness);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "fill_mode"), (float)params.fill_mode);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "fill_color"), &color);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "fill_color2"), &color2);
	gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "fill_gradient"), &gradient);
	gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "uv_scale"), &uv_scale);
	gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "uv_offset"), &uv_offset);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "opacity"), params.opacity);

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	if (image) {
		if (params.texture) {
			if (linear_srgb)
				gs_effect_set_texture_srgb(image, params.texture);
			else
				gs_effect_set_texture(image, params.texture);
		} else {
			gs_effect_set_texture(image, nullptr);
		}
	}

	gs_matrix_push();

	const bool transformed = std::abs(params.rotation) > 0.001f || std::abs(params.scale - 1.0f) > 0.0001f;

	if (transformed) {
		/* Rotation and pulse both work about the element's centre. */
		gs_matrix_translate3f(params.x + params.width * 0.5f, params.y + params.height * 0.5f, 0.0f);

		if (std::abs(params.rotation) > 0.001f)
			gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, params.rotation * PI / 180.0f);

		if (std::abs(params.scale - 1.0f) > 0.0001f)
			gs_matrix_scale3f(params.scale, params.scale, 1.0f);

		gs_matrix_translate3f(-params.width * 0.5f, -params.height * 0.5f, 0.0f);
	} else {
		gs_matrix_translate3f(params.x, params.y, 0.0f);
	}

	gs_technique_t *technique = gs_effect_get_technique(effect, sprite ? "Sprite" : "Panel");

	gs_technique_begin(technique);
	gs_technique_begin_pass(technique, 0);

	gs_draw_sprite(nullptr, 0, (uint32_t)std::lround(params.width), (uint32_t)std::lround(params.height));

	gs_technique_end_pass(technique);
	gs_technique_end(technique);

	gs_matrix_pop();
}

void destroy_texture(gs_texture_t *&texture)
{
	if (!texture)
		return;

	obs_enter_graphics();
	gs_texture_destroy(texture);
	obs_leave_graphics();

	texture = nullptr;
}

/* video_tick runs on the graphics thread but outside its context, so the
 * context has to be taken explicitly before touching a texture. */
gs_texture_t *create_texture(const RgbaImage &image)
{
	if (!image.valid())
		return nullptr;

	const uint8_t *data = image.pixels.data();

	obs_enter_graphics();
	gs_texture_t *texture = gs_texture_create(image.width, image.height, GS_RGBA, 1, &data, 0);
	obs_leave_graphics();

	return texture;
}

bool current_accent(const MusicWidget *widget, uint32_t &color)
{
	if (widget->fixed_accent) {
		color = widget->fixed_color;
		return true;
	}

	color = widget->accent;
	return widget->accent_valid;
}

uint32_t resolve_color(const MusicWidget *widget, uint32_t color, bool follow_accent)
{
	uint32_t accent = 0;
	if (!follow_accent || !current_accent(widget, accent))
		return color;

	/* The accent replaces the colour but keeps whatever alpha was set, so
	 * a translucent panel stays translucent. */
	return (accent & 0x00FFFFFF) | (color & 0xFF000000);
}

} // namespace vr::widget
