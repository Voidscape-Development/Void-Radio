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

#include "sources/progress-bar-source.hpp"
#include "music/music-api.hpp"
#include "util/vr-util.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace vr {

namespace {

enum class FillType {
	Color = 0,
	Gradient = 1,
	Image = 2,
	Source = 3,
};

enum class Direction {
	LeftToRight = 0,
	RightToLeft = 1,
	BottomToTop = 2,
	TopToBottom = 3,
	CenterOut = 4,
};

enum class ResetStyle {
	FillThenReset = 0,
	EaseBack = 1,
	Instant = 2,
};

enum class IdleMode {
	Empty = 0,
	Hide = 1,
	Sweep = 2,
};

/* One drawable layer of the bar: the track behind the fill, or the fill. */
struct Layer {
	FillType type = FillType::Color;
	uint32_t color = 0xFF1E1E1E;
	uint32_t color2 = 0xFF000000;
	float gradient_angle = 0.0f;
	float opacity = 1.0f;
	bool tile = false;

	std::string image_path;
	gs_image_file4_t image = {};
	bool image_valid = false;

	std::string source_name;
	obs_weak_source_t *weak_source = nullptr;
	gs_texrender_t *texrender = nullptr;

	/* Filled in during render. */
	gs_texture_t *texture = nullptr;
	uint32_t texture_cx = 0;
	uint32_t texture_cy = 0;
};

struct ProgressBar {
	obs_source_t *self = nullptr;
	gs_effect_t *effect = nullptr;

	uint32_t width = 480;
	uint32_t height = 24;
	Direction direction = Direction::LeftToRight;
	float corner_radius = 6.0f;
	float border_width = 0.0f;
	uint32_t border_color = 0xFF000000;

	Layer background;
	Layer foreground;

	std::string music_source_name;
	obs_weak_source_t *music_weak = nullptr;

	ResetStyle reset_style = ResetStyle::FillThenReset;
	float reset_seconds = 0.3f;
	IdleMode idle_mode = IdleMode::Empty;

	/* Animation state. */
	uint64_t last_serial = 0;
	bool have_serial = false;
	float displayed = 0.0f;
	float target = 0.0f;
	bool animating = false;
	float animation_time = 0.0f;
	float animation_from = 0.0f;
	float sweep_position = 0.0f;
	bool idle = true;
	bool visible = true;
};

float ease_in_out(float t)
{
	t = std::min(std::max(t, 0.0f), 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

void release_layer_source(Layer &layer)
{
	if (layer.weak_source) {
		obs_weak_source_release(layer.weak_source);
		layer.weak_source = nullptr;
	}
}

void free_layer_graphics(Layer &layer)
{
	obs_enter_graphics();

	if (layer.image_valid) {
		gs_image_file4_free(&layer.image);
		layer.image_valid = false;
	}

	if (layer.texrender) {
		gs_texrender_destroy(layer.texrender);
		layer.texrender = nullptr;
	}

	obs_leave_graphics();
}

void load_layer_image(Layer &layer, const char *path)
{
	const std::string next = path ? path : "";

	if (layer.image_valid && next == layer.image_path)
		return;

	if (layer.image_valid) {
		obs_enter_graphics();
		gs_image_file4_free(&layer.image);
		obs_leave_graphics();
		layer.image_valid = false;
	}

	layer.image_path = next;

	if (layer.image_path.empty())
		return;

	gs_image_file4_init(&layer.image, layer.image_path.c_str(), GS_IMAGE_ALPHA_STRAIGHT);

	obs_enter_graphics();
	gs_image_file4_init_texture(&layer.image);
	obs_leave_graphics();

	layer.image_valid = layer.image.image3.image2.image.loaded;

	if (!layer.image_valid)
		obs_log(LOG_WARNING, "progress bar could not load image '%s'", layer.image_path.c_str());
}

/* Binds the layer to another source, refusing anything that would make the
 * bar render itself. */
void set_layer_source(ProgressBar *bar, Layer &layer, const char *name)
{
	const std::string next = name ? name : "";

	if (next == layer.source_name && layer.weak_source)
		return;

	release_layer_source(layer);
	layer.source_name = next;

	if (layer.source_name.empty())
		return;

	obs_source_t *source = obs_get_source_by_name(layer.source_name.c_str());
	if (!source)
		return;

	if (source == bar->self) {
		obs_source_release(source);
		layer.source_name.clear();
		return;
	}

	layer.weak_source = obs_source_get_weak_source(source);
	obs_source_release(source);
}

void update_layer(ProgressBar *bar, Layer &layer, obs_data_t *settings, const char *prefix)
{
	const auto key = [prefix](const char *suffix) {
		return std::string(prefix) + suffix;
	};

	layer.type = (FillType)obs_data_get_int(settings, key("type").c_str());
	layer.color = (uint32_t)obs_data_get_int(settings, key("color").c_str());
	layer.color2 = (uint32_t)obs_data_get_int(settings, key("color2").c_str());
	layer.gradient_angle = (float)obs_data_get_double(settings, key("gradient_angle").c_str());
	layer.opacity = (float)obs_data_get_double(settings, key("opacity").c_str()) / 100.0f;
	layer.tile = obs_data_get_bool(settings, key("tile").c_str());

	if (layer.type == FillType::Image)
		load_layer_image(layer, obs_data_get_string(settings, key("image").c_str()));

	if (layer.type == FillType::Source)
		set_layer_source(bar, layer, obs_data_get_string(settings, key("source").c_str()));
	else
		release_layer_source(layer);
}

/* Renders a bound source into the layer's own texture so the shader can sample
 * it. Returns nothing; the result lands in layer.texture. */
void render_layer_source(Layer &layer, uint32_t width, uint32_t height)
{
	if (!layer.weak_source)
		return;

	obs_source_t *source = obs_weak_source_get_source(layer.weak_source);
	if (!source)
		return;

	const uint32_t cx = std::max(obs_source_get_width(source), 1u);
	const uint32_t cy = std::max(obs_source_get_height(source), 1u);

	if (!layer.texrender)
		layer.texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

	gs_texrender_reset(layer.texrender);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin(layer.texrender, cx, cy)) {
		struct vec4 clear_color;
		vec4_zero(&clear_color);

		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

		obs_source_video_render(source);

		gs_texrender_end(layer.texrender);

		layer.texture = gs_texrender_get_texture(layer.texrender);
		layer.texture_cx = cx;
		layer.texture_cy = cy;
	}

	gs_blend_state_pop();

	obs_source_release(source);

	UNUSED_PARAMETER(width);
	UNUSED_PARAMETER(height);
}

void resolve_layer_texture(Layer &layer, uint32_t width, uint32_t height)
{
	layer.texture = nullptr;
	layer.texture_cx = 0;
	layer.texture_cy = 0;

	if (layer.type == FillType::Image && layer.image_valid) {
		layer.texture = layer.image.image3.image2.image.texture;
		layer.texture_cx = layer.image.image3.image2.image.cx;
		layer.texture_cy = layer.image.image3.image2.image.cy;
	} else if (layer.type == FillType::Source) {
		render_layer_source(layer, width, height);
	}
}

void set_layer_params(gs_effect_t *effect, const Layer &layer, const char *prefix, uint32_t width, uint32_t height,
		      bool linear_srgb)
{
	const auto param = [effect, prefix](const char *suffix) {
		const std::string name = std::string(prefix) + suffix;
		return gs_effect_get_param_by_name(effect, name.c_str());
	};

	float mode = 0.0f;
	switch (layer.type) {
	case FillType::Gradient:
		mode = 1.0f;
		break;
	case FillType::Image:
	case FillType::Source:
		mode = layer.texture ? 2.0f : 0.0f;
		break;
	default:
		mode = 0.0f;
		break;
	}

	struct vec4 color;
	struct vec4 color2;

	if (linear_srgb) {
		vec4_from_rgba_srgb(&color, layer.color);
		vec4_from_rgba_srgb(&color2, layer.color2);
	} else {
		vec4_from_rgba(&color, layer.color);
		vec4_from_rgba(&color2, layer.color2);
	}

	/* A texture layer is tinted by its colour, so white leaves it alone. */
	if (mode >= 1.5f) {
		struct vec4 tint;
		vec4_set(&tint, 1.0f, 1.0f, 1.0f, color.w);
		color = tint;
	}

	const float radians = layer.gradient_angle * (float)M_PI / 180.0f;
	struct vec2 gradient;
	vec2_set(&gradient, std::cos(radians), std::sin(radians));

	struct vec2 uv_scale;
	if (layer.tile && layer.texture_cx && layer.texture_cy) {
		vec2_set(&uv_scale, (float)width / (float)layer.texture_cx, (float)height / (float)layer.texture_cy);
	} else {
		vec2_set(&uv_scale, 1.0f, 1.0f);
	}

	gs_effect_set_float(param("_mode"), mode);
	gs_effect_set_vec4(param("_color"), &color);
	gs_effect_set_vec4(param("_color2"), &color2);
	gs_effect_set_vec2(param("_gradient"), &gradient);
	gs_effect_set_vec2(param("_uv_scale"), &uv_scale);
	gs_effect_set_float(param("_opacity"), layer.opacity);

	gs_eparam_t *texture_param = param("_tex");
	if (texture_param) {
		if (layer.texture) {
			if (linear_srgb)
				gs_effect_set_texture_srgb(texture_param, layer.texture);
			else
				gs_effect_set_texture(texture_param, layer.texture);
		} else {
			gs_effect_set_texture(texture_param, nullptr);
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Source callbacks                                                           */
/* ------------------------------------------------------------------------- */

const char *bar_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("ProgressBar");
}

void bar_update(void *data, obs_data_t *settings)
{
	ProgressBar *bar = static_cast<ProgressBar *>(data);

	bar->width = (uint32_t)std::max<int64_t>(1, obs_data_get_int(settings, "width"));
	bar->height = (uint32_t)std::max<int64_t>(1, obs_data_get_int(settings, "height"));
	bar->direction = (Direction)obs_data_get_int(settings, "direction");
	bar->corner_radius = (float)obs_data_get_double(settings, "corner_radius");
	bar->border_width = (float)obs_data_get_double(settings, "border_width");
	bar->border_color = (uint32_t)obs_data_get_int(settings, "border_color");
	bar->reset_style = (ResetStyle)obs_data_get_int(settings, "reset_style");
	bar->reset_seconds = (float)obs_data_get_int(settings, "reset_ms") / 1000.0f;
	bar->idle_mode = (IdleMode)obs_data_get_int(settings, "idle_mode");

	const char *music_name = obs_data_get_string(settings, "music_source");

	if (bar->music_source_name != music_name || !bar->music_weak) {
		bar->music_source_name = music_name ? music_name : "";

		if (bar->music_weak) {
			obs_weak_source_release(bar->music_weak);
			bar->music_weak = nullptr;
		}

		if (!bar->music_source_name.empty()) {
			obs_source_t *source = obs_get_source_by_name(bar->music_source_name.c_str());
			if (source) {
				if (is_music_source(source))
					bar->music_weak = obs_source_get_weak_source(source);
				obs_source_release(source);
			}
		}
	}

	update_layer(bar, bar->background, settings, "bg_");
	update_layer(bar, bar->foreground, settings, "fg_");
}

void *bar_create(obs_data_t *settings, obs_source_t *source)
{
	ProgressBar *bar = new ProgressBar();
	bar->self = source;

	char *effect_path = obs_module_file("effects/progress-bar.effect");

	obs_enter_graphics();
	bar->effect = gs_effect_create_from_file(effect_path, nullptr);
	obs_leave_graphics();

	bfree(effect_path);

	if (!bar->effect)
		obs_log(LOG_ERROR, "failed to load effects/progress-bar.effect");

	bar_update(bar, settings);

	return bar;
}

void bar_destroy(void *data)
{
	ProgressBar *bar = static_cast<ProgressBar *>(data);

	free_layer_graphics(bar->background);
	free_layer_graphics(bar->foreground);
	release_layer_source(bar->background);
	release_layer_source(bar->foreground);

	if (bar->music_weak)
		obs_weak_source_release(bar->music_weak);

	if (bar->effect) {
		obs_enter_graphics();
		gs_effect_destroy(bar->effect);
		obs_leave_graphics();
	}

	delete bar;
}

uint32_t bar_get_width(void *data)
{
	return static_cast<ProgressBar *>(data)->width;
}

uint32_t bar_get_height(void *data)
{
	return static_cast<ProgressBar *>(data)->height;
}

void bar_tick(void *data, float seconds)
{
	ProgressBar *bar = static_cast<ProgressBar *>(data);

	const uint64_t elapsed_ns = (uint64_t)(seconds * 1000000000.0f);

	for (Layer *layer : {&bar->background, &bar->foreground}) {
		if (!layer->image_valid)
			continue;

		/* Animated images only advance when their texture is refreshed. */
		if (gs_image_file4_tick(&layer->image, elapsed_ns)) {
			obs_enter_graphics();
			gs_image_file4_update_texture(&layer->image);
			obs_leave_graphics();
		}
	}

	Snapshot snapshot;
	bool have_snapshot = false;

	if (bar->music_weak) {
		obs_source_t *source = obs_weak_source_get_source(bar->music_weak);
		if (source) {
			have_snapshot = get_snapshot(source, snapshot);
			obs_source_release(source);
		}
	}

	const bool playing = have_snapshot && snapshot.state != PlayState::Stopped;
	bar->idle = !playing || snapshot.duration_ms <= 0;

	if (bar->idle) {
		bar->have_serial = false;
		bar->animating = false;
		bar->displayed = 0.0f;
		bar->target = 0.0f;

		if (bar->idle_mode == IdleMode::Sweep) {
			bar->sweep_position += seconds * 0.6f;
			if (bar->sweep_position > 1.35f)
				bar->sweep_position = -0.35f;
		}

		return;
	}

	bar->target = std::min(std::max(snapshot.progress, 0.0f), 1.0f);

	/* A new track restarts the bar. How it gets back to the start is the
	 * user's choice; the animation always lands exactly on the live value
	 * so there is no jump when it finishes. */
	if (!bar->have_serial) {
		bar->have_serial = true;
		bar->last_serial = snapshot.track_serial;
		bar->displayed = bar->target;
	} else if (snapshot.track_serial != bar->last_serial) {
		bar->last_serial = snapshot.track_serial;

		if (bar->reset_style == ResetStyle::Instant || bar->reset_seconds <= 0.0f) {
			bar->displayed = bar->target;
		} else {
			bar->animating = true;
			bar->animation_time = 0.0f;
			bar->animation_from = bar->displayed;
		}
	}

	if (!bar->animating) {
		bar->displayed = bar->target;
		return;
	}

	bar->animation_time += seconds;

	const float t = std::min(bar->animation_time / bar->reset_seconds, 1.0f);

	if (bar->reset_style == ResetStyle::FillThenReset) {
		if (t < 0.5f) {
			const float phase = ease_in_out(t / 0.5f);
			bar->displayed = bar->animation_from + (1.0f - bar->animation_from) * phase;
		} else {
			const float phase = ease_in_out((t - 0.5f) / 0.5f);
			bar->displayed = 1.0f + (bar->target - 1.0f) * phase;
		}
	} else {
		const float phase = ease_in_out(t);
		bar->displayed = bar->animation_from + (bar->target - bar->animation_from) * phase;
	}

	if (t >= 1.0f) {
		bar->animating = false;
		bar->displayed = bar->target;
	}
}

void bar_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);

	ProgressBar *bar = static_cast<ProgressBar *>(data);

	if (!bar->effect)
		return;

	if (bar->idle && bar->idle_mode == IdleMode::Hide)
		return;

	resolve_layer_texture(bar->background, bar->width, bar->height);
	resolve_layer_texture(bar->foreground, bar->width, bar->height);

	const bool linear_srgb = gs_get_linear_srgb();
	const bool previous_srgb = gs_framebuffer_srgb_enabled();

	gs_enable_framebuffer_srgb(linear_srgb);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	struct vec2 size;
	vec2_set(&size, (float)bar->width, (float)bar->height);

	struct vec4 border;
	if (linear_srgb)
		vec4_from_rgba_srgb(&border, bar->border_color);
	else
		vec4_from_rgba(&border, bar->border_color);

	const bool sweeping = bar->idle && bar->idle_mode == IdleMode::Sweep;

	gs_effect_set_vec2(gs_effect_get_param_by_name(bar->effect, "bar_size"), &size);
	gs_effect_set_float(gs_effect_get_param_by_name(bar->effect, "corner_radius"), bar->corner_radius);
	gs_effect_set_float(gs_effect_get_param_by_name(bar->effect, "border_width"), bar->border_width);
	gs_effect_set_vec4(gs_effect_get_param_by_name(bar->effect, "border_color"), &border);
	gs_effect_set_float(gs_effect_get_param_by_name(bar->effect, "progress"), bar->displayed);
	gs_effect_set_float(gs_effect_get_param_by_name(bar->effect, "direction"), (float)bar->direction);
	gs_effect_set_float(gs_effect_get_param_by_name(bar->effect, "sweep_active"), sweeping ? 1.0f : 0.0f);
	gs_effect_set_float(gs_effect_get_param_by_name(bar->effect, "sweep_pos"), bar->sweep_position);
	gs_effect_set_float(gs_effect_get_param_by_name(bar->effect, "sweep_half"), 0.12f);

	set_layer_params(bar->effect, bar->background, "bg", bar->width, bar->height, linear_srgb);
	set_layer_params(bar->effect, bar->foreground, "fg", bar->width, bar->height, linear_srgb);

	gs_technique_t *technique = gs_effect_get_technique(bar->effect, "Draw");

	gs_technique_begin(technique);
	gs_technique_begin_pass(technique, 0);

	gs_draw_sprite(nullptr, 0, bar->width, bar->height);

	gs_technique_end_pass(technique);
	gs_technique_end(technique);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);
}

void bar_enum_active_sources(void *data, obs_source_enum_proc_t callback, void *param)
{
	ProgressBar *bar = static_cast<ProgressBar *>(data);

	for (Layer *layer : {&bar->background, &bar->foreground}) {
		if (layer->type != FillType::Source || !layer->weak_source)
			continue;

		obs_source_t *source = obs_weak_source_get_source(layer->weak_source);
		if (source) {
			callback(bar->self, source, param);
			obs_source_release(source);
		}
	}
}

void bar_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 480);
	obs_data_set_default_int(settings, "height", 24);
	obs_data_set_default_int(settings, "direction", (int64_t)Direction::LeftToRight);
	obs_data_set_default_double(settings, "corner_radius", 6.0);
	obs_data_set_default_double(settings, "border_width", 0.0);
	obs_data_set_default_int(settings, "border_color", 0xFF000000);
	obs_data_set_default_int(settings, "reset_style", (int64_t)ResetStyle::FillThenReset);
	obs_data_set_default_int(settings, "reset_ms", 300);
	obs_data_set_default_int(settings, "idle_mode", (int64_t)IdleMode::Empty);

	obs_data_set_default_int(settings, "bg_type", (int64_t)FillType::Color);
	obs_data_set_default_int(settings, "bg_color", 0xC0000000);
	obs_data_set_default_int(settings, "bg_color2", 0xC0303030);
	obs_data_set_default_double(settings, "bg_gradient_angle", 0.0);
	obs_data_set_default_double(settings, "bg_opacity", 100.0);
	obs_data_set_default_bool(settings, "bg_tile", false);

	obs_data_set_default_int(settings, "fg_type", (int64_t)FillType::Color);
	obs_data_set_default_int(settings, "fg_color", 0xFF4CC2FF);
	obs_data_set_default_int(settings, "fg_color2", 0xFF9B5CFF);
	obs_data_set_default_double(settings, "fg_gradient_angle", 0.0);
	obs_data_set_default_double(settings, "fg_opacity", 100.0);
	obs_data_set_default_bool(settings, "fg_tile", false);
}

bool music_source_list(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(settings);

	obs_property_list_clear(property);
	obs_property_list_add_string(property, obs_module_text("Common.None"), "");

	std::vector<obs_source_t *> sources;
	enum_music_sources(sources);

	for (obs_source_t *source : sources) {
		const char *name = obs_source_get_name(source);
		if (name)
			obs_property_list_add_string(property, name, name);

		obs_source_release(source);
	}

	return true;
}

bool texture_source_list(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(settings);

	obs_property_list_clear(property);
	obs_property_list_add_string(property, obs_module_text("Common.None"), "");

	auto collect = [](void *param, obs_source_t *source) -> bool {
		obs_property_t *list = static_cast<obs_property_t *>(param);

		const uint32_t flags = obs_source_get_output_flags(source);
		if ((flags & OBS_SOURCE_VIDEO) == 0)
			return true;

		const char *name = obs_source_get_name(source);
		if (name)
			obs_property_list_add_string(list, name, name);

		return true;
	};

	obs_enum_sources(collect, property);

	return true;
}

/* Shows only the controls that matter for the fill type in use. */
bool fill_type_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	const char *name = obs_property_name(property);
	const std::string prefix = std::string(name).substr(0, 3); /* "bg_" or "fg_" */

	const FillType type = (FillType)obs_data_get_int(settings, name);

	const auto set_visible = [&](const char *suffix, bool visible) {
		obs_property_t *target = obs_properties_get(props, (prefix + suffix).c_str());
		if (target)
			obs_property_set_visible(target, visible);
	};

	set_visible("color", true);
	set_visible("color2", type == FillType::Gradient);
	set_visible("gradient_angle", type == FillType::Gradient);
	set_visible("image", type == FillType::Image);
	set_visible("source", type == FillType::Source);
	set_visible("tile", type == FillType::Image || type == FillType::Source);

	return true;
}

void add_layer_properties(obs_properties_t *props, const char *prefix, const char *group_text)
{
	obs_properties_t *group = obs_properties_create();

	const auto key = [prefix](const char *suffix) {
		return std::string(prefix) + suffix;
	};

	obs_property_t *type = obs_properties_add_list(group, key("type").c_str(), obs_module_text("Bar.FillType"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Color"), (int64_t)FillType::Color);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Gradient"), (int64_t)FillType::Gradient);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Image"), (int64_t)FillType::Image);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Source"), (int64_t)FillType::Source);
	obs_property_set_modified_callback(type, fill_type_modified);

	obs_properties_add_color_alpha(group, key("color").c_str(), obs_module_text("Bar.Color"));
	obs_properties_add_color_alpha(group, key("color2").c_str(), obs_module_text("Bar.Color2"));

	obs_property_t *angle = obs_properties_add_float_slider(group, key("gradient_angle").c_str(),
								obs_module_text("Bar.GradientAngle"), 0.0, 360.0, 1.0);
	obs_property_float_set_suffix(angle, "°");

	obs_properties_add_path(group, key("image").c_str(), obs_module_text("Bar.Image"), OBS_PATH_FILE,
				obs_module_text("Bar.Image.Filter"), nullptr);

	obs_property_t *source = obs_properties_add_list(group, key("source").c_str(), obs_module_text("Bar.Source"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	texture_source_list(group, source, nullptr);

	obs_properties_add_bool(group, key("tile").c_str(), obs_module_text("Bar.Tile"));

	obs_property_t *opacity = obs_properties_add_float_slider(group, key("opacity").c_str(),
								  obs_module_text("Bar.Opacity"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(opacity, "%");

	obs_properties_add_group(props, (std::string(prefix) + "group").c_str(), group_text, OBS_GROUP_NORMAL, group);
}

obs_properties_t *bar_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_property_t *music = obs_properties_add_list(props, "music_source", obs_module_text("Common.MusicSource"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	music_source_list(props, music, nullptr);

	obs_properties_add_int(props, "width", obs_module_text("Bar.Width"), 1, 8192, 1);
	obs_properties_add_int(props, "height", obs_module_text("Bar.Height"), 1, 8192, 1);

	obs_property_t *direction = obs_properties_add_list(props, "direction", obs_module_text("Bar.Direction"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.LeftToRight"),
				  (int64_t)Direction::LeftToRight);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.RightToLeft"),
				  (int64_t)Direction::RightToLeft);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.BottomToTop"),
				  (int64_t)Direction::BottomToTop);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.TopToBottom"),
				  (int64_t)Direction::TopToBottom);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.CenterOut"), (int64_t)Direction::CenterOut);

	obs_properties_add_float_slider(props, "corner_radius", obs_module_text("Bar.CornerRadius"), 0.0, 512.0, 1.0);
	obs_properties_add_float_slider(props, "border_width", obs_module_text("Bar.BorderWidth"), 0.0, 64.0, 1.0);
	obs_properties_add_color_alpha(props, "border_color", obs_module_text("Bar.BorderColor"));

	add_layer_properties(props, "bg_", obs_module_text("Bar.Background"));
	add_layer_properties(props, "fg_", obs_module_text("Bar.Foreground"));

	obs_property_t *reset = obs_properties_add_list(props, "reset_style", obs_module_text("Bar.ResetStyle"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(reset, obs_module_text("Bar.ResetStyle.FillThenReset"),
				  (int64_t)ResetStyle::FillThenReset);
	obs_property_list_add_int(reset, obs_module_text("Bar.ResetStyle.EaseBack"), (int64_t)ResetStyle::EaseBack);
	obs_property_list_add_int(reset, obs_module_text("Bar.ResetStyle.Instant"), (int64_t)ResetStyle::Instant);

	obs_property_t *reset_ms =
		obs_properties_add_int_slider(props, "reset_ms", obs_module_text("Bar.ResetDuration"), 0, 3000, 10);
	obs_property_int_set_suffix(reset_ms, " ms");

	obs_property_t *idle = obs_properties_add_list(props, "idle_mode", obs_module_text("Bar.Idle"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(idle, obs_module_text("Bar.Idle.Empty"), (int64_t)IdleMode::Empty);
	obs_property_list_add_int(idle, obs_module_text("Bar.Idle.Hide"), (int64_t)IdleMode::Hide);
	obs_property_list_add_int(idle, obs_module_text("Bar.Idle.Sweep"), (int64_t)IdleMode::Sweep);
	obs_property_set_long_description(idle, obs_module_text("Bar.Idle.Description"));

	return props;
}

struct obs_source_info progress_bar_info = {};

} // namespace

void register_progress_bar_source()
{
	progress_bar_info.id = PROGRESS_BAR_SOURCE_ID;
	progress_bar_info.type = OBS_SOURCE_TYPE_INPUT;
	progress_bar_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
	progress_bar_info.icon_type = OBS_ICON_TYPE_MEDIA;
	progress_bar_info.get_name = bar_get_name;
	progress_bar_info.create = bar_create;
	progress_bar_info.destroy = bar_destroy;
	progress_bar_info.update = bar_update;
	progress_bar_info.get_defaults = bar_defaults;
	progress_bar_info.get_properties = bar_properties;
	progress_bar_info.get_width = bar_get_width;
	progress_bar_info.get_height = bar_get_height;
	progress_bar_info.video_tick = bar_tick;
	progress_bar_info.video_render = bar_render;
	progress_bar_info.enum_active_sources = bar_enum_active_sources;

	obs_register_source(&progress_bar_info);
}

} // namespace vr
