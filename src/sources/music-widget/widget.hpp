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

#include "music/music-api.hpp"
#include "render/bar-renderer.hpp"
#include "render/image-file-compat.hpp"
#include "render/image-util.hpp"
#include "render/text-render.hpp"
#include "sources/music-widget/art-loader.hpp"
#include "util/text-template.hpp"

#include <obs-module.h>
#include <graphics/graphics.h>

#include <cstdint>
#include <string>

/*
 * Internals of the Music Widget source, shared between its files:
 *
 *   music-widget-source.cpp   lifecycle, the per-frame tick, registration
 *   widget-settings.cpp       reading settings, defaults and layout presets
 *   widget-properties.cpp     the properties dialog
 *   widget-render.cpp         drawing every element and the track transition
 *   widget-draw.cpp           layout maths and the shared quad/texture helpers
 *   art-loader.cpp            decoding and blurring cover art off-thread
 *
 * A self contained now-playing card.
 *
 * Everything the widget shows is an element placed on its own canvas: the
 * backdrop, the album art, six independent lines of template text, and a
 * progress bar borrowed from the standalone bar source. Each element picks a
 * corner or edge of the canvas to anchor to and is nudged from there in
 * pixels, so resizing the widget keeps a layout roughly where it was put.
 *
 * The whole card is composited into a texture of its own every frame. That
 * costs one extra render target, and buys the track change transitions: the
 * previous frame is kept around and the two are blended.
 */

namespace vr::widget {

constexpr float PI = 3.14159265358979323846f;

constexpr int TEXT_SLOTS = 6;

enum class Anchor {
	TopLeft = 0,
	TopCenter = 1,
	TopRight = 2,
	MiddleLeft = 3,
	Center = 4,
	MiddleRight = 5,
	BottomLeft = 6,
	BottomCenter = 7,
	BottomRight = 8,
};

enum class BackgroundType {
	Color = 0,
	Gradient = 1,
	Image = 2,
	BlurredArt = 3,
};

enum class ArtFit {
	Cover = 0,
	Contain = 1,
	Stretch = 2,
};

enum class ArtAnimation {
	None = 0,
	Spin = 1,
	Pulse = 2,
};

enum class TrackTransition {
	None = 0,
	CrossFade = 1,
	Slide = 2,
	FadeThroughBlank = 3,
};

enum class WidgetIdle {
	Hide = 0,
	Placeholder = 1,
	Freeze = 2,
};

/* What counts as "nothing playing". A paused track is still the current track,
 * so keeping the card as it was is the default. */
enum class WidgetIdleTrigger {
	Stopped = 0,
	StoppedOrPaused = 1,
};

/* ------------------------------------------------------------------------- */
/* Element state                                                              */
/* ------------------------------------------------------------------------- */

struct Geometry {
	bool enabled = true;
	Anchor anchor = Anchor::TopLeft;
	float x = 0.0f;
	float y = 0.0f;
	float width = 100.0f;
	float height = 100.0f;
	float opacity = 1.0f;
	int z = 0;
};

struct TextElement {
	Geometry geometry;

	std::string format;
	std::string idle_format;
	bool hide_empty = true;
	bool follow_accent = false;
	float rotation = 0.0f;

	TextStyle style;

	/* Rasterised state, only redone when something it depends on moves. */
	std::string raster_text;
	TextStyle raster_style;
	uint32_t raster_width = 0;
	uint32_t raster_height = 0;
	RasterizedText raster;
	gs_texture_t *texture = nullptr;
	bool needs_upload = false;

	float scroll_time = 0.0f;

	/* The string as of the last tick, kept so the raster can be reused. */
	std::string current;
};

struct ArtElement {
	Geometry geometry;

	ArtFit fit = ArtFit::Cover;
	float corner_radius = 8.0f;
	float border_width = 0.0f;
	uint32_t border_color = 0xFF000000;
	bool border_accent = false;

	bool shadow = false;
	float shadow_x = 0.0f;
	float shadow_y = 6.0f;
	float shadow_blur = 12.0f;
	uint32_t shadow_color = 0xA0000000;

	ArtAnimation animation = ArtAnimation::None;
	float rotation = 0.0f;
	float spin_rpm = 12.0f;
	float pulse_percent = 4.0f;
	float pulse_rate = 1.0f;

	std::string placeholder;

	float spin_angle = 0.0f;
	float pulse_time = 0.0f;
};

struct BackgroundElement {
	bool enabled = true;
	BackgroundType type = BackgroundType::Color;

	uint32_t color = 0xC0141414;
	uint32_t color2 = 0xC0303030;
	float gradient_angle = 90.0f;
	bool follow_accent = false;

	std::string image_path;

	float blur = 70.0f;
	float darken = 45.0f;
	float desaturate = 20.0f;

	float inset = 0.0f;
	float corner_radius = 12.0f;
	float border_width = 0.0f;
	uint32_t border_color = 0xFF000000;
	bool border_accent = false;
	float opacity = 1.0f;
	int z = -100;

	/* Only used by the image fill; the blurred art comes from the loader. */
	vr_image_file_t image = {};
	bool image_valid = false;
};

struct DrawParams {
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;

	float corner_radius = 0.0f;
	float border_width = 0.0f;
	uint32_t border_color = 0;
	float softness = 0.0f;

	/* 0 colour, 1 gradient, 2 texture. */
	int fill_mode = 0;
	uint32_t color = 0xFFFFFFFF;
	uint32_t color2 = 0xFFFFFFFF;
	float gradient_angle = 0.0f;

	gs_texture_t *texture = nullptr;
	float uv_scale_x = 1.0f;
	float uv_scale_y = 1.0f;
	float uv_offset_x = 0.0f;
	float uv_offset_y = 0.0f;

	float opacity = 1.0f;
	float rotation = 0.0f;
	float scale = 1.0f;
};

/* ------------------------------------------------------------------------- */
/* The source                                                                 */
/* ------------------------------------------------------------------------- */

struct MusicWidget {
	obs_source_t *self = nullptr;
	gs_effect_t *effect = nullptr;

	uint32_t width = 640;
	uint32_t height = 180;

	MusicLink link;

	TimeFormat time_format = TimeFormat::Auto;

	BackgroundElement background;
	ArtElement art;
	TextElement text[TEXT_SLOTS];

	Geometry bar_geometry;
	BarRenderer bar;

	TrackTransition transition = TrackTransition::CrossFade;
	float transition_seconds = 0.35f;
	WidgetIdle idle_mode = WidgetIdle::Placeholder;
	WidgetIdleTrigger idle_trigger = WidgetIdleTrigger::Stopped;

	/* Playback state as of the last tick. */
	Snapshot snapshot;
	bool have_snapshot = false;
	bool idle = true;

	/* Kept so that "freeze on the last track" has something to freeze on. */
	Snapshot frozen;
	bool have_frozen = false;

	uint64_t last_serial = 0;
	bool have_serial = false;

	bool transitioning = false;
	float transition_time = 0.0f;

	ArtLoader loader;
	std::string requested_path;
	std::string requested_placeholder;
	bool art_settings_dirty = true;

	RgbaImage pending_art;
	RgbaImage pending_blur;
	bool upload_art = false;
	bool upload_blur = false;
	bool clear_art = false;

	gs_texture_t *art_texture = nullptr;
	gs_texture_t *blur_texture = nullptr;

	/* Taken from the cover art, unless the colour source is a fixed colour,
	 * which then stands in for it everywhere. */
	uint32_t accent = 0;
	bool accent_valid = false;
	bool fixed_accent = false;
	uint32_t fixed_color = 0;

	gs_texrender_t *composite = nullptr;
	gs_texture_t *previous = nullptr;
	uint32_t previous_width = 0;
	uint32_t previous_height = 0;
	bool previous_valid = false;
};

/* Shared between the widget's files; everything else is local to one. */
std::string key_for(const std::string &prefix, const char *suffix);
std::string text_prefix(int slot);
void resolve_position(const Geometry &geometry, uint32_t canvas_width, uint32_t canvas_height, float &x, float &y);
void draw_quad(gs_effect_t *effect, const DrawParams &params, bool sprite);
void destroy_texture(gs_texture_t *&texture);
gs_texture_t *create_texture(const RgbaImage &image);
uint32_t resolve_color(const MusicWidget *widget, uint32_t color, bool follow_accent);
bool current_accent(const MusicWidget *widget, uint32_t &color);
void widget_update(void *data, obs_data_t *settings);
void widget_defaults(obs_data_t *settings);
obs_properties_t *widget_properties(void *data);
void widget_render(void *data, gs_effect_t *unused);
void widget_enum_active_sources(void *data, obs_source_enum_proc_t callback, void *param);

} // namespace vr::widget
