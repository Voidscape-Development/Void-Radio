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
#include "sources/music-widget/spectrum.hpp"
#include "sources/music-widget/widget-palette.hpp"
#include "util/text-template.hpp"

#include <obs-module.h>
#include <graphics/graphics.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

/*
 * Internals of the Music Widget source, shared between its files:
 *
 *   music-widget-source.cpp   lifecycle, the per-frame tick, registration
 *   widget-settings.cpp       reading settings, defaults and layout presets
 *   widget-properties.cpp     the properties dialog
 *   widget-render.cpp         drawing every element and the track transition
 *   widget-draw.cpp           layout maths and the shared quad/texture helpers
 *   widget-extras.cpp         the logo image, play state icon and blurred art
 *   widget-visualizer.cpp     the audio visualiser and its audio tap
 *   widget-motion.cpp         showing on track change, and element entrances
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

/* Whether the card is always on screen, or shows itself for a while when a
 * track starts and then gets out of the way. */
enum class PopupMode {
	Always = 0,
	OnTrackChange = 1,
};

enum class PopupAnimation {
	Fade = 0,
	SlideUp = 1,
	SlideDown = 2,
	SlideLeft = 3,
	SlideRight = 4,
	Scale = 5,
};

/* How each element arrives when a track starts, one after another. */
enum class EnterStyle {
	None = 0,
	Fade = 1,
	SlideUp = 2,
	SlideLeft = 3,
	Scale = 4,
};

enum class VizMode {
	Bars = 0,
	Mirror = 1,
	Wave = 2,
	Ring = 3,
};

/* What the play state icon shows. */
enum class IconShow {
	Always = 0,
	NotPlaying = 1, /* only while paused or stopped */
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
	AccentTone tone = AccentTone::Accent;
	bool shadow_accent = false;
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
	AccentTone tone = AccentTone::Accent;
	bool follow_accent2 = false;
	AccentTone tone2 = AccentTone::Dark;

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

/* An audio visualiser, drawn from the linked music source's own output. */
struct VisualizerElement {
	Geometry geometry;

	VizMode mode = VizMode::Bars;
	int bands = 32;
	float gap = 30.0f; /* percent of each bar's slot left empty */
	bool rounded = true;
	float line_width = 3.0f;  /* the wave's thickness */
	float ring_inner = 70.0f; /* percent of the box the ring starts at */

	uint32_t color = 0xFFFFFFFF;
	uint32_t color2 = 0xFFFFFFFF;
	bool gradient = false;
	bool follow_accent = true;
	AccentTone tone = AccentTone::Accent;

	float sensitivity = 100.0f; /* percent */
	float smoothing = 60.0f;    /* percent */
	float min_freq = 40.0f;
	float max_freq = 16000.0f;

	/* What is drawn: 0..1 per band, and -1..1 per point for the wave. */
	std::vector<float> levels;
	std::vector<float> wave;
	float time = 0.0f;

	/* Paused audio stops arriving rather than going quiet, so the frame
	 * count standing still is how silence is noticed. */
	uint64_t last_frames = 0;
	float stale = 0.0f;
};

/* A picture of the user's own, such as a channel logo. */
struct ImageElement {
	Geometry geometry;
	ArtFit fit = ArtFit::Contain;
	float corner_radius = 0.0f;

	std::string path;
	vr_image_file_t image = {};
	bool image_valid = false;
};

/* Play, pause or stop, as a glyph. */
struct IconElement {
	Geometry geometry;
	uint32_t color = 0xFFFFFFFF;
	bool follow_accent = false;
	AccentTone tone = AccentTone::Accent;
	IconShow show = IconShow::Always;

	/* White masks tinted when drawn, rebuilt when the box size changes. */
	gs_texture_t *play = nullptr;
	gs_texture_t *pause = nullptr;
	gs_texture_t *stop = nullptr;
	uint32_t texture_size = 0;
};

/* The cover blurred, inside a box of its own: a soft glow behind the art. */
struct GlowElement {
	Geometry geometry;
	float corner_radius = 24.0f;
	float softness = 16.0f;
};

/* The audio the visualiser looks at, captured on OBS's audio thread and read
 * on the graphics thread. */
struct AudioTap {
	std::mutex mutex;
	std::vector<float> ring = std::vector<float>(SPECTRUM_WINDOW, 0.0f);
	size_t write = 0;
	uint64_t frames = 0; /* total captured, so silence can be told apart */
	uint32_t sample_rate = 48000;

	obs_weak_source_t *attached = nullptr;
};

/* A made-up track the layout editor can show while designing, so the card can
 * be laid out with nothing playing. */
struct PreviewTrack {
	bool active = false;
	bool long_text = false;
	uint64_t serial = 1;
	float time = 0.0f;
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

	VisualizerElement viz;
	ImageElement image;
	IconElement icon;
	GlowElement glow;

	AudioTap tap;
	Spectrum spectrum;

	TrackTransition transition = TrackTransition::CrossFade;
	float transition_seconds = 0.35f;
	WidgetIdle idle_mode = WidgetIdle::Placeholder;
	WidgetIdleTrigger idle_trigger = WidgetIdleTrigger::Stopped;

	PopupMode popup_mode = PopupMode::Always;
	PopupAnimation popup_animation = PopupAnimation::SlideUp;
	float popup_hold = 8.0f;    /* seconds fully shown */
	float popup_seconds = 0.4f; /* each way */
	float popup_time = 1e9f;    /* since it was last asked to show */
	float popup_level = 1.0f;   /* 0 hidden .. 1 shown, after easing */
	std::atomic<bool> popup_request{false};
	obs_hotkey_id popup_hotkey = OBS_INVALID_HOTKEY_ID;

	EnterStyle enter_style = EnterStyle::None;
	float enter_seconds = 0.45f;
	float enter_stagger = 0.07f;
	float enter_time = 1e9f;

	/* Multiplied into every element's opacity while it is drawn, for the
	 * entrance animation. */
	float draw_opacity = 1.0f;

	std::mutex preview_mutex;
	PreviewTrack preview;
	bool replay_request = false; /* under preview_mutex */
	Snapshot preview_snapshot;

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
	int requested_sample = 0;
	int sample_cover = 0; /* which sample cover to show, 0 for the real one */
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
	Palette palette;

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
uint32_t resolve_color(const MusicWidget *widget, uint32_t color, bool follow_accent,
		       AccentTone tone = AccentTone::Accent);
bool current_accent(const MusicWidget *widget, uint32_t &color);
void refresh_palette(MusicWidget *widget);
void draw_texture_box(MusicWidget *widget, gs_texture_t *texture, const Geometry &geometry, ArtFit fit,
		      float corner_radius, float softness, uint32_t tint);

/* widget-extras.cpp */
void read_extras(MusicWidget *widget, obs_data_t *settings);
void tick_extras(MusicWidget *widget);
void draw_image(MusicWidget *widget);
void draw_icon(MusicWidget *widget, PlayState state);
void draw_glow(MusicWidget *widget);
void destroy_extras(MusicWidget *widget);

/* widget-visualizer.cpp */
void read_visualizer(MusicWidget *widget, obs_data_t *settings);
void tick_visualizer(MusicWidget *widget, float seconds, bool playing, bool preview);
void draw_visualizer(MusicWidget *widget);
void detach_audio(MusicWidget *widget);

/* widget-motion.cpp */
void read_motion(MusicWidget *widget, obs_data_t *settings);
void register_motion_hotkey(MusicWidget *widget);
void unregister_motion_hotkey(MusicWidget *widget);
void trigger_popup(MusicWidget *widget);
void trigger_entrance(MusicWidget *widget);
void tick_motion(MusicWidget *widget, float seconds);

/* How far an element is through its entrance: opacity, offset and scale. */
struct Reveal {
	float opacity = 1.0f;
	float dx = 0.0f;
	float dy = 0.0f;
	float scale = 1.0f;
};
Reveal element_reveal(const MusicWidget *widget, int order);

/* The whole card as the pop-in has it: opacity, offset and scale. */
Reveal popup_reveal(const MusicWidget *widget);
void widget_update(void *data, obs_data_t *settings);
void widget_defaults(obs_data_t *settings);
obs_properties_t *widget_properties(void *data);
void widget_render(void *data, gs_effect_t *unused);
void widget_enum_active_sources(void *data, obs_source_enum_proc_t callback, void *param);

} // namespace vr::widget
