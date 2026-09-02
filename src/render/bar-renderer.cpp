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

#include "render/bar-renderer.hpp"

#include <plugin-support.h>
#include <graphics/matrix4.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vr {

namespace {

/* Spelled out rather than taken from <cmath>, which does not define M_PI on
 * every toolchain. */
constexpr float PI = 3.14159265358979323846f;

float ease_in_out(float t)
{
	t = std::min(std::max(t, 0.0f), 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

void release_layer_source(FillLayer &layer)
{
	if (layer.weak_source) {
		obs_weak_source_release(layer.weak_source);
		layer.weak_source = nullptr;
	}
}

void free_layer_graphics(FillLayer &layer)
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

void load_layer_image(FillLayer &layer, const char *path)
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
void set_layer_source(obs_source_t *self, FillLayer &layer, const char *name)
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

	if (source == self) {
		obs_source_release(source);
		layer.source_name.clear();
		return;
	}

	layer.weak_source = obs_source_get_weak_source(source);
	obs_source_release(source);
}

/* Renders a bound source into the layer's own texture so the shader can sample
 * it. The result lands in layer.texture. */
void render_layer_source(FillLayer &layer)
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
}

/* Shows only the controls that matter for the fill type in use. The prefix is
 * whatever precedes "type" in the property name, so this works no matter what
 * the owning source called its group. */
bool fill_type_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	const std::string name = obs_property_name(property);
	if (name.size() < 4)
		return false;

	const std::string prefix = name.substr(0, name.size() - 4);
	const FillType type = (FillType)obs_data_get_int(settings, name.c_str());

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

void texture_source_list(obs_property_t *property)
{
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
}

void add_layer_properties(obs_properties_t *props, const std::string &prefix, const char *group_text,
			  bool include_accent)
{
	obs_properties_t *group = obs_properties_create();

	const auto key = [&prefix](const char *suffix) {
		return prefix + suffix;
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
	texture_source_list(source);

	obs_properties_add_bool(group, key("tile").c_str(), obs_module_text("Bar.Tile"));

	if (include_accent) {
		obs_property_t *accent =
			obs_properties_add_bool(group, key("accent").c_str(), obs_module_text("Bar.FollowAccent"));
		obs_property_set_long_description(accent, obs_module_text("Bar.FollowAccent.Description"));
	}

	obs_property_t *opacity = obs_properties_add_float_slider(group, key("opacity").c_str(),
								  obs_module_text("Bar.Opacity"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(opacity, "%");

	obs_properties_add_group(props, (prefix + "group").c_str(), group_text, OBS_GROUP_NORMAL, group);
}

} // namespace

BarRenderer::~BarRenderer()
{
	destroy();
}

void BarRenderer::create()
{
	if (effect_)
		return;

	char *effect_path = obs_module_file("effects/progress-bar.effect");

	obs_enter_graphics();
	effect_ = gs_effect_create_from_file(effect_path, nullptr);
	obs_leave_graphics();

	bfree(effect_path);

	if (!effect_)
		obs_log(LOG_ERROR, "failed to load effects/progress-bar.effect");
}

void BarRenderer::destroy()
{
	free_layer_graphics(background_);
	free_layer_graphics(foreground_);
	release_layer_source(background_);
	release_layer_source(foreground_);

	if (effect_) {
		obs_enter_graphics();
		gs_effect_destroy(effect_);
		obs_leave_graphics();
		effect_ = nullptr;
	}
}

void BarRenderer::set_accent(uint32_t color, bool valid)
{
	accent_ = color;
	accent_valid_ = valid;
}

void BarRenderer::update_layer(obs_source_t *self, FillLayer &layer, obs_data_t *settings, const std::string &prefix)
{
	const auto key = [&prefix](const char *suffix) {
		return prefix + suffix;
	};

	layer.type = (FillType)obs_data_get_int(settings, key("type").c_str());
	layer.color = (uint32_t)obs_data_get_int(settings, key("color").c_str());
	layer.color2 = (uint32_t)obs_data_get_int(settings, key("color2").c_str());
	layer.gradient_angle = (float)obs_data_get_double(settings, key("gradient_angle").c_str());
	layer.opacity = (float)obs_data_get_double(settings, key("opacity").c_str()) / 100.0f;
	layer.tile = obs_data_get_bool(settings, key("tile").c_str());
	layer.follow_accent = obs_data_get_bool(settings, key("accent").c_str());

	if (layer.type == FillType::Image)
		load_layer_image(layer, obs_data_get_string(settings, key("image").c_str()));

	if (layer.type == FillType::Source)
		set_layer_source(self, layer, obs_data_get_string(settings, key("source").c_str()));
	else
		release_layer_source(layer);
}

void BarRenderer::update(obs_source_t *self, obs_data_t *settings, const std::string &prefix)
{
	const auto key = [&prefix](const char *suffix) {
		return prefix + suffix;
	};

	width_ = (uint32_t)std::max<int64_t>(1, obs_data_get_int(settings, key("width").c_str()));
	height_ = (uint32_t)std::max<int64_t>(1, obs_data_get_int(settings, key("height").c_str()));
	direction_ = (BarDirection)obs_data_get_int(settings, key("direction").c_str());
	corner_radius_ = (float)obs_data_get_double(settings, key("corner_radius").c_str());
	border_width_ = (float)obs_data_get_double(settings, key("border_width").c_str());
	border_color_ = (uint32_t)obs_data_get_int(settings, key("border_color").c_str());
	border_follow_accent_ = obs_data_get_bool(settings, key("border_accent").c_str());
	reset_style_ = (ResetStyle)obs_data_get_int(settings, key("reset_style").c_str());
	reset_seconds_ = (float)obs_data_get_int(settings, key("reset_ms").c_str()) / 1000.0f;
	idle_mode_ = (IdleMode)obs_data_get_int(settings, key("idle_mode").c_str());
	idle_trigger_ = (IdleTrigger)obs_data_get_int(settings, key("idle_trigger").c_str());

	update_layer(self, background_, settings, prefix + "bg_");
	update_layer(self, foreground_, settings, prefix + "fg_");
}

void BarRenderer::begin_reset(float to)
{
	if (reset_style_ == ResetStyle::Instant || reset_seconds_ <= 0.0f) {
		animating_ = false;
		displayed_ = to;
		return;
	}

	animating_ = true;
	animation_time_ = 0.0f;
	animation_from_ = displayed_;
}

void BarRenderer::tick(float seconds, const Snapshot *snapshot)
{
	/* A track that has started but not yet said how long it is gets this
	 * long before the bar gives up and calls itself idle. Long enough to
	 * cover the handful of polls a decoder takes to report a duration,
	 * short enough that a live stream settles into its idle look at once. */
	constexpr float LENGTH_GRACE_SECONDS = 1.0f;

	const uint64_t elapsed_ns = (uint64_t)(seconds * 1000000000.0f);

	for (FillLayer *layer : {&background_, &foreground_}) {
		if (!layer->image_valid)
			continue;

		/* Animated images only advance when their texture is refreshed. */
		if (gs_image_file4_tick(&layer->image, elapsed_ns)) {
			obs_enter_graphics();
			gs_image_file4_update_texture(&layer->image);
			obs_leave_graphics();
		}
	}

	const bool stopped = !snapshot || snapshot->state == PlayState::Stopped ||
			     (idle_trigger_ == IdleTrigger::StoppedOrPaused && snapshot->state == PlayState::Paused);
	const bool have_length = !stopped && snapshot->duration_ms > 0;

	/*
	 * The track change is spotted from the serial alone, before anything
	 * about lengths is decided. A freshly started track reports no duration
	 * for a poll or two, and reading that gap as "gone idle" is what used
	 * to reset the animation state and make the bar snap to the new track
	 * instead of playing the chosen animation.
	 */
	if (!stopped) {
		if (!have_serial_) {
			have_serial_ = true;
			last_serial_ = snapshot->track_serial;
			animating_ = false;
			displayed_ = have_length ? std::min(std::max(snapshot->progress, 0.0f), 1.0f) : 0.0f;
		} else if (snapshot->track_serial != last_serial_) {
			last_serial_ = snapshot->track_serial;
			length_wait_ = 0.0f;
			begin_reset(0.0f);
		}
	} else if (have_serial_) {
		/* Playback ending runs the same animation, down to an empty bar,
		 * rather than dropping it there in one frame. */
		have_serial_ = false;
		begin_reset(0.0f);
	}

	length_wait_ = have_length || stopped ? 0.0f : length_wait_ + seconds;

	idle_ = stopped || (!have_length && length_wait_ >= LENGTH_GRACE_SECONDS);

	if (idle_ && idle_mode_ == IdleMode::Sweep) {
		sweep_position_ += seconds * 0.6f;
		if (sweep_position_ > 1.35f)
			sweep_position_ = -0.35f;
	}

	target_ = have_length ? std::min(std::max(snapshot->progress, 0.0f), 1.0f) : 0.0f;

	if (!animating_) {
		displayed_ = target_;
		return;
	}

	animation_time_ += seconds;

	const float t = std::min(animation_time_ / reset_seconds_, 1.0f);

	if (reset_style_ == ResetStyle::FillThenReset) {
		if (t < 0.5f) {
			const float phase = ease_in_out(t / 0.5f);
			displayed_ = animation_from_ + (1.0f - animation_from_) * phase;
		} else {
			const float phase = ease_in_out((t - 0.5f) / 0.5f);
			displayed_ = 1.0f + (target_ - 1.0f) * phase;
		}
	} else {
		const float phase = ease_in_out(t);
		displayed_ = animation_from_ + (target_ - animation_from_) * phase;
	}

	if (t >= 1.0f) {
		animating_ = false;
		displayed_ = target_;
	}
}

void BarRenderer::resolve_layer_texture(FillLayer &layer, uint32_t width, uint32_t height)
{
	UNUSED_PARAMETER(width);
	UNUSED_PARAMETER(height);

	layer.texture = nullptr;
	layer.texture_cx = 0;
	layer.texture_cy = 0;

	if (layer.type == FillType::Image && layer.image_valid) {
		layer.texture = layer.image.image3.image2.image.texture;
		layer.texture_cx = layer.image.image3.image2.image.cx;
		layer.texture_cy = layer.image.image3.image2.image.cy;
	} else if (layer.type == FillType::Source) {
		render_layer_source(layer);
	}
}

void BarRenderer::set_layer_params(const FillLayer &layer, const char *prefix, uint32_t width, uint32_t height,
				   bool linear_srgb) const
{
	const auto param = [this, prefix](const char *suffix) {
		const std::string name = std::string(prefix) + suffix;
		return gs_effect_get_param_by_name(effect_, name.c_str());
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

	const uint32_t base = (layer.follow_accent && accent_valid_) ? accent_ : layer.color;

	struct vec4 color;
	struct vec4 color2;

	if (linear_srgb) {
		vec4_from_rgba_srgb(&color, base);
		vec4_from_rgba_srgb(&color2, layer.color2);
	} else {
		vec4_from_rgba(&color, base);
		vec4_from_rgba(&color2, layer.color2);
	}

	/* A texture layer is tinted by its colour, so white leaves it alone. */
	if (mode >= 1.5f) {
		struct vec4 tint;
		vec4_set(&tint, 1.0f, 1.0f, 1.0f, color.w);
		color = tint;
	}

	const float radians = layer.gradient_angle * PI / 180.0f;
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

void BarRenderer::prepare(uint32_t width, uint32_t height)
{
	if (!effect_ || width == 0 || height == 0)
		return;

	resolve_layer_texture(background_, width, height);
	resolve_layer_texture(foreground_, width, height);
}

void BarRenderer::render(uint32_t width, uint32_t height)
{
	if (!effect_ || width == 0 || height == 0)
		return;

	const bool linear_srgb = gs_get_linear_srgb();
	const bool previous_srgb = gs_framebuffer_srgb_enabled();

	gs_enable_framebuffer_srgb(linear_srgb);

	gs_blend_state_push();

	/* Alpha accumulates rather than being replaced, so the bar composites
	 * correctly when the Music Widget draws it into a target of its own. */
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	struct vec2 size;
	vec2_set(&size, (float)width, (float)height);

	const uint32_t border = (border_follow_accent_ && accent_valid_) ? accent_ : border_color_;

	struct vec4 border_color;
	if (linear_srgb)
		vec4_from_rgba_srgb(&border_color, border);
	else
		vec4_from_rgba(&border_color, border);

	const bool sweeping = idle_ && idle_mode_ == IdleMode::Sweep;

	gs_effect_set_vec2(gs_effect_get_param_by_name(effect_, "bar_size"), &size);
	gs_effect_set_float(gs_effect_get_param_by_name(effect_, "corner_radius"), corner_radius_);
	gs_effect_set_float(gs_effect_get_param_by_name(effect_, "border_width"), border_width_);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect_, "border_color"), &border_color);
	gs_effect_set_float(gs_effect_get_param_by_name(effect_, "progress"), displayed_);
	gs_effect_set_float(gs_effect_get_param_by_name(effect_, "direction"), (float)direction_);
	gs_effect_set_float(gs_effect_get_param_by_name(effect_, "sweep_active"), sweeping ? 1.0f : 0.0f);
	gs_effect_set_float(gs_effect_get_param_by_name(effect_, "sweep_pos"), sweep_position_);
	gs_effect_set_float(gs_effect_get_param_by_name(effect_, "sweep_half"), 0.12f);

	set_layer_params(background_, "bg", width, height, linear_srgb);
	set_layer_params(foreground_, "fg", width, height, linear_srgb);

	gs_technique_t *technique = gs_effect_get_technique(effect_, "Draw");

	gs_technique_begin(technique);
	gs_technique_begin_pass(technique, 0);

	gs_draw_sprite(nullptr, 0, width, height);

	gs_technique_end_pass(technique);
	gs_technique_end(technique);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);
}

void BarRenderer::enum_active_sources(obs_source_t *self, obs_source_enum_proc_t callback, void *param) const
{
	for (const FillLayer *layer : {&background_, &foreground_}) {
		if (layer->type != FillType::Source || !layer->weak_source)
			continue;

		obs_source_t *source = obs_weak_source_get_source(layer->weak_source);
		if (source) {
			callback(self, source, param);
			obs_source_release(source);
		}
	}
}

void BarRenderer::add_properties(obs_properties_t *props, const std::string &prefix, bool include_size,
				 bool include_accent)
{
	const auto key = [&prefix](const char *suffix) {
		return prefix + suffix;
	};

	if (include_size) {
		obs_properties_add_int(props, key("width").c_str(), obs_module_text("Bar.Width"), 1, 8192, 1);
		obs_properties_add_int(props, key("height").c_str(), obs_module_text("Bar.Height"), 1, 8192, 1);
	}

	obs_property_t *direction = obs_properties_add_list(props, key("direction").c_str(),
							    obs_module_text("Bar.Direction"), OBS_COMBO_TYPE_LIST,
							    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.LeftToRight"),
				  (int64_t)BarDirection::LeftToRight);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.RightToLeft"),
				  (int64_t)BarDirection::RightToLeft);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.BottomToTop"),
				  (int64_t)BarDirection::BottomToTop);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.TopToBottom"),
				  (int64_t)BarDirection::TopToBottom);
	obs_property_list_add_int(direction, obs_module_text("Bar.Direction.CenterOut"),
				  (int64_t)BarDirection::CenterOut);

	obs_properties_add_float_slider(props, key("corner_radius").c_str(), obs_module_text("Bar.CornerRadius"), 0.0,
					512.0, 1.0);
	obs_properties_add_float_slider(props, key("border_width").c_str(), obs_module_text("Bar.BorderWidth"), 0.0,
					64.0, 1.0);
	obs_properties_add_color_alpha(props, key("border_color").c_str(), obs_module_text("Bar.BorderColor"));

	if (include_accent)
		obs_properties_add_bool(props, key("border_accent").c_str(), obs_module_text("Bar.BorderFollowAccent"));

	add_layer_properties(props, prefix + "bg_", obs_module_text("Bar.Background"), include_accent);
	add_layer_properties(props, prefix + "fg_", obs_module_text("Bar.Foreground"), include_accent);

	obs_property_t *reset = obs_properties_add_list(props, key("reset_style").c_str(),
							obs_module_text("Bar.ResetStyle"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(reset, obs_module_text("Bar.ResetStyle.FillThenReset"),
				  (int64_t)ResetStyle::FillThenReset);
	obs_property_list_add_int(reset, obs_module_text("Bar.ResetStyle.EaseBack"), (int64_t)ResetStyle::EaseBack);
	obs_property_list_add_int(reset, obs_module_text("Bar.ResetStyle.Instant"), (int64_t)ResetStyle::Instant);

	obs_property_t *reset_ms = obs_properties_add_int_slider(props, key("reset_ms").c_str(),
								 obs_module_text("Bar.ResetDuration"), 0, 3000, 10);
	obs_property_int_set_suffix(reset_ms, " ms");

	obs_property_t *idle = obs_properties_add_list(props, key("idle_mode").c_str(), obs_module_text("Bar.Idle"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(idle, obs_module_text("Bar.Idle.Empty"), (int64_t)IdleMode::Empty);
	obs_property_list_add_int(idle, obs_module_text("Bar.Idle.Hide"), (int64_t)IdleMode::Hide);
	obs_property_list_add_int(idle, obs_module_text("Bar.Idle.Sweep"), (int64_t)IdleMode::Sweep);
	obs_property_set_long_description(idle, obs_module_text("Bar.Idle.Description"));

	obs_property_t *trigger = obs_properties_add_list(props, key("idle_trigger").c_str(),
							  obs_module_text("Common.IdleTrigger"), OBS_COMBO_TYPE_LIST,
							  OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(trigger, obs_module_text("Common.IdleTrigger.Stopped"),
				  (int64_t)IdleTrigger::Stopped);
	obs_property_list_add_int(trigger, obs_module_text("Common.IdleTrigger.StoppedOrPaused"),
				  (int64_t)IdleTrigger::StoppedOrPaused);
	obs_property_set_long_description(trigger, obs_module_text("Common.IdleTrigger.Description"));
}

void BarRenderer::add_defaults(obs_data_t *settings, const std::string &prefix)
{
	const auto key = [&prefix](const char *suffix) {
		return prefix + suffix;
	};

	obs_data_set_default_int(settings, key("width").c_str(), 480);
	obs_data_set_default_int(settings, key("height").c_str(), 24);
	obs_data_set_default_int(settings, key("direction").c_str(), (int64_t)BarDirection::LeftToRight);
	obs_data_set_default_double(settings, key("corner_radius").c_str(), 6.0);
	obs_data_set_default_double(settings, key("border_width").c_str(), 0.0);
	obs_data_set_default_int(settings, key("border_color").c_str(), 0xFF000000);
	obs_data_set_default_bool(settings, key("border_accent").c_str(), false);
	obs_data_set_default_int(settings, key("reset_style").c_str(), (int64_t)ResetStyle::FillThenReset);
	obs_data_set_default_int(settings, key("reset_ms").c_str(), 300);
	obs_data_set_default_int(settings, key("idle_mode").c_str(), (int64_t)IdleMode::Empty);
	obs_data_set_default_int(settings, key("idle_trigger").c_str(), (int64_t)IdleTrigger::Stopped);

	obs_data_set_default_int(settings, key("bg_type").c_str(), (int64_t)FillType::Color);
	obs_data_set_default_int(settings, key("bg_color").c_str(), 0xC0000000);
	obs_data_set_default_int(settings, key("bg_color2").c_str(), 0xC0303030);
	obs_data_set_default_double(settings, key("bg_gradient_angle").c_str(), 0.0);
	obs_data_set_default_double(settings, key("bg_opacity").c_str(), 100.0);
	obs_data_set_default_bool(settings, key("bg_tile").c_str(), false);
	obs_data_set_default_bool(settings, key("bg_accent").c_str(), false);

	obs_data_set_default_int(settings, key("fg_type").c_str(), (int64_t)FillType::Color);
	obs_data_set_default_int(settings, key("fg_color").c_str(), 0xFF4CC2FF);
	obs_data_set_default_int(settings, key("fg_color2").c_str(), 0xFF9B5CFF);
	obs_data_set_default_double(settings, key("fg_gradient_angle").c_str(), 0.0);
	obs_data_set_default_double(settings, key("fg_opacity").c_str(), 100.0);
	obs_data_set_default_bool(settings, key("fg_tile").c_str(), false);
	obs_data_set_default_bool(settings, key("fg_accent").c_str(), false);
}

} // namespace vr
