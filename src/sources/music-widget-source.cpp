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

#include "sources/music-widget-source.hpp"
#include "music/album-art.hpp"
#include "music/music-api.hpp"
#include "render/bar-renderer.hpp"
#include "render/image-util.hpp"
#include "render/text-render.hpp"
#include "util/text-template.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/matrix4.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/*
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

namespace vr {

namespace {

constexpr float PI = 3.14159265358979323846f;

constexpr int TEXT_SLOTS = 6;

/* Cover art is never shown large enough to justify keeping the original
 * around, and the smaller copy makes the accent colour pass cheap. */
constexpr uint32_t MAX_ART_DIMENSION = 1024;

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

enum class LayoutPreset {
	ArtLeft = 0,
	ArtRight = 1,
	VerticalCard = 2,
	WideBanner = 3,
	CompactBar = 4,
	TextOnly = 5,
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
	gs_image_file4_t image = {};
	bool image_valid = false;
};

/* ------------------------------------------------------------------------- */
/* Cover art loading                                                          */
/* ------------------------------------------------------------------------- */

/*
 * Decoding a JPEG and blurring it are far too slow for the graphics thread, so
 * they happen on a worker. The widget posts a request whenever the track or
 * the backdrop settings change and picks the result up on a later tick.
 */
class ArtLoader {
public:
	struct Request {
		std::string path;
		std::string placeholder;
		bool want_blur = false;
		uint32_t blur_width = 0;
		uint32_t blur_height = 0;
		float blur_strength = 0.0f;
		float blur_darken = 0.0f;
		float blur_desaturate = 0.0f;
	};

	struct Result {
		RgbaImage art;
		RgbaImage blur;
		uint32_t accent = 0;
		bool accent_valid = false;
		bool have_art = false;
		bool have_blur = false;
	};

	void start()
	{
		if (thread_.joinable())
			return;

		thread_ = std::thread([this]() { run(); });
	}

	void stop()
	{
		if (!thread_.joinable())
			return;

		{
			std::lock_guard<std::mutex> lock(mutex_);
			quit_ = true;
		}

		condition_.notify_all();
		thread_.join();
	}

	void request(const Request &next)
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);

			/* Only the newest request matters; anything queued
			 * behind it is already out of date. */
			pending_ = next;
			have_pending_ = true;
		}

		condition_.notify_one();
	}

	/* Returns true and fills `out` exactly once per completed request. */
	bool take(Result &out)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!have_result_)
			return false;

		out = std::move(result_);
		have_result_ = false;

		return true;
	}

private:
	void run()
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

	Result process(const Request &request)
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
			result.have_blur = blur_image(decoded_, request.blur_width, request.blur_height,
						      request.blur_strength, request.blur_darken,
						      request.blur_desaturate, result.blur);
		}

		return result;
	}

	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable condition_;

	bool quit_ = false;
	bool have_pending_ = false;
	Request pending_;

	bool have_result_ = false;
	Result result_;

	/* Worker private: the decoded cover is kept so that changing only the
	 * backdrop settings re-blurs without decoding again. */
	std::string decoded_key_ = "\n";
	RgbaImage decoded_;
	uint32_t decoded_accent_ = 0;
	bool decoded_accent_valid_ = false;
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

	uint32_t accent = 0;
	bool accent_valid = false;

	gs_texrender_t *composite = nullptr;
	gs_texture_t *previous = nullptr;
	uint32_t previous_width = 0;
	uint32_t previous_height = 0;
	bool previous_valid = false;
};

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

float anchor_fraction_x(Anchor anchor)
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

float anchor_fraction_y(Anchor anchor)
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

uint32_t resolve_color(const MusicWidget *widget, uint32_t color, bool follow_accent)
{
	if (!follow_accent || !widget->accent_valid)
		return color;

	/* The accent replaces the colour but keeps whatever alpha was set, so
	 * a translucent panel stays translucent. */
	return (widget->accent & 0x00FFFFFF) | (color & 0xFF000000);
}

/* ------------------------------------------------------------------------- */
/* Settings                                                                   */
/* ------------------------------------------------------------------------- */

void read_geometry(Geometry &geometry, obs_data_t *settings, const std::string &prefix, bool read_size)
{
	geometry.enabled = obs_data_get_bool(settings, key_for(prefix, "enable").c_str());
	geometry.anchor = (Anchor)obs_data_get_int(settings, key_for(prefix, "anchor").c_str());
	geometry.x = (float)obs_data_get_double(settings, key_for(prefix, "x").c_str());
	geometry.y = (float)obs_data_get_double(settings, key_for(prefix, "y").c_str());
	geometry.opacity = (float)obs_data_get_double(settings, key_for(prefix, "opacity").c_str()) / 100.0f;
	geometry.z = (int)obs_data_get_int(settings, key_for(prefix, "z").c_str());

	if (read_size) {
		geometry.width =
			(float)std::max<int64_t>(1, obs_data_get_int(settings, key_for(prefix, "width").c_str()));
		geometry.height =
			(float)std::max<int64_t>(1, obs_data_get_int(settings, key_for(prefix, "height").c_str()));
	}
}

void read_text_style(TextStyle &style, obs_data_t *settings, const std::string &prefix)
{
	obs_data_t *font = obs_data_get_obj(settings, key_for(prefix, "font").c_str());

	if (font) {
		const char *face = obs_data_get_string(font, "face");
		const char *face_style = obs_data_get_string(font, "style");

		style.font_family = face ? face : "Arial";
		style.font_style = face_style ? face_style : "";
		style.font_size = (int)std::max<int64_t>(1, obs_data_get_int(font, "size"));

		const uint32_t flags = (uint32_t)obs_data_get_int(font, "flags");
		style.bold = (flags & OBS_FONT_BOLD) != 0;
		style.italic = (flags & OBS_FONT_ITALIC) != 0;
		style.underline = (flags & OBS_FONT_UNDERLINE) != 0;

		obs_data_release(font);
	}

	style.color = (uint32_t)obs_data_get_int(settings, key_for(prefix, "color").c_str());
	style.color2 = (uint32_t)obs_data_get_int(settings, key_for(prefix, "color2").c_str());
	style.gradient = obs_data_get_bool(settings, key_for(prefix, "gradient").c_str());
	style.gradient_angle = (float)obs_data_get_double(settings, key_for(prefix, "gradient_angle").c_str());

	style.outline_width = (float)obs_data_get_double(settings, key_for(prefix, "outline").c_str());
	style.outline_color = (uint32_t)obs_data_get_int(settings, key_for(prefix, "outline_color").c_str());

	style.shadow = obs_data_get_bool(settings, key_for(prefix, "shadow").c_str());
	style.shadow_x = (float)obs_data_get_double(settings, key_for(prefix, "shadow_x").c_str());
	style.shadow_y = (float)obs_data_get_double(settings, key_for(prefix, "shadow_y").c_str());
	style.shadow_blur = (float)obs_data_get_double(settings, key_for(prefix, "shadow_blur").c_str());
	style.shadow_color = (uint32_t)obs_data_get_int(settings, key_for(prefix, "shadow_color").c_str());

	style.letter_spacing = (float)obs_data_get_double(settings, key_for(prefix, "letter_spacing").c_str());
	style.line_height = (float)obs_data_get_double(settings, key_for(prefix, "line_height").c_str()) / 100.0f;

	style.transform = (TextTransform)obs_data_get_int(settings, key_for(prefix, "transform").c_str());
	style.align = (TextAlign)obs_data_get_int(settings, key_for(prefix, "align").c_str());
	style.valign = (TextVAlign)obs_data_get_int(settings, key_for(prefix, "valign").c_str());

	style.overflow = (TextOverflow)obs_data_get_int(settings, key_for(prefix, "overflow").c_str());
	style.marquee_style = (MarqueeStyle)obs_data_get_int(settings, key_for(prefix, "marquee_style").c_str());
	style.ellipsis_side = (EllipsisSide)obs_data_get_int(settings, key_for(prefix, "ellipsis_side").c_str());
	style.max_lines = (int)std::max<int64_t>(1, obs_data_get_int(settings, key_for(prefix, "max_lines").c_str()));
	style.shrink_min = (float)obs_data_get_double(settings, key_for(prefix, "shrink_min").c_str());
	style.marquee_gap = (float)obs_data_get_double(settings, key_for(prefix, "marquee_gap").c_str());
	style.marquee_speed = (float)obs_data_get_double(settings, key_for(prefix, "marquee_speed").c_str());
	style.marquee_pause = (float)obs_data_get_double(settings, key_for(prefix, "marquee_pause").c_str());
}

void load_background_image(BackgroundElement &background, const char *path)
{
	const std::string next = path ? path : "";

	if (background.image_valid && next == background.image_path)
		return;

	if (background.image_valid) {
		obs_enter_graphics();
		gs_image_file4_free(&background.image);
		obs_leave_graphics();
		background.image_valid = false;
	}

	background.image_path = next;

	if (background.image_path.empty())
		return;

	gs_image_file4_init(&background.image, background.image_path.c_str(), GS_IMAGE_ALPHA_STRAIGHT);

	obs_enter_graphics();
	gs_image_file4_init_texture(&background.image);
	obs_leave_graphics();

	background.image_valid = background.image.image3.image2.image.loaded;

	if (!background.image_valid)
		obs_log(LOG_WARNING, "music widget could not load background image '%s'",
			background.image_path.c_str());
}

void widget_update(void *data, obs_data_t *settings)
{
	MusicWidget *widget = static_cast<MusicWidget *>(data);

	widget->width = (uint32_t)std::max<int64_t>(1, obs_data_get_int(settings, "width"));
	widget->height = (uint32_t)std::max<int64_t>(1, obs_data_get_int(settings, "height"));
	widget->time_format = (TimeFormat)obs_data_get_int(settings, "time_format");

	widget->transition = (TrackTransition)obs_data_get_int(settings, "transition");
	widget->transition_seconds = (float)obs_data_get_int(settings, "transition_ms") / 1000.0f;
	widget->idle_mode = (WidgetIdle)obs_data_get_int(settings, "idle_mode");
	widget->idle_trigger = (WidgetIdleTrigger)obs_data_get_int(settings, "idle_trigger");

	widget->link.update(settings);

	/* Background. */
	BackgroundElement &background = widget->background;

	background.enabled = obs_data_get_bool(settings, "bg_enable");
	background.type = (BackgroundType)obs_data_get_int(settings, "bg_type");
	background.color = (uint32_t)obs_data_get_int(settings, "bg_color");
	background.color2 = (uint32_t)obs_data_get_int(settings, "bg_color2");
	background.gradient_angle = (float)obs_data_get_double(settings, "bg_gradient_angle");
	background.follow_accent = obs_data_get_bool(settings, "bg_accent");
	background.blur = (float)obs_data_get_double(settings, "bg_blur");
	background.darken = (float)obs_data_get_double(settings, "bg_darken");
	background.desaturate = (float)obs_data_get_double(settings, "bg_desaturate");
	background.inset = (float)obs_data_get_double(settings, "bg_inset");
	background.corner_radius = (float)obs_data_get_double(settings, "bg_corner_radius");
	background.border_width = (float)obs_data_get_double(settings, "bg_border_width");
	background.border_color = (uint32_t)obs_data_get_int(settings, "bg_border_color");
	background.border_accent = obs_data_get_bool(settings, "bg_border_accent");
	background.opacity = (float)obs_data_get_double(settings, "bg_opacity") / 100.0f;
	background.z = (int)obs_data_get_int(settings, "bg_z");

	if (background.type == BackgroundType::Image)
		load_background_image(background, obs_data_get_string(settings, "bg_image"));

	/* Album art. */
	ArtElement &art = widget->art;

	read_geometry(art.geometry, settings, "art_", true);
	art.fit = (ArtFit)obs_data_get_int(settings, "art_fit");
	art.corner_radius = (float)obs_data_get_double(settings, "art_corner_radius");
	art.border_width = (float)obs_data_get_double(settings, "art_border_width");
	art.border_color = (uint32_t)obs_data_get_int(settings, "art_border_color");
	art.border_accent = obs_data_get_bool(settings, "art_border_accent");
	art.shadow = obs_data_get_bool(settings, "art_shadow");
	art.shadow_x = (float)obs_data_get_double(settings, "art_shadow_x");
	art.shadow_y = (float)obs_data_get_double(settings, "art_shadow_y");
	art.shadow_blur = (float)obs_data_get_double(settings, "art_shadow_blur");
	art.shadow_color = (uint32_t)obs_data_get_int(settings, "art_shadow_color");
	art.animation = (ArtAnimation)obs_data_get_int(settings, "art_animation");
	art.rotation = (float)obs_data_get_double(settings, "art_rotation");
	art.spin_rpm = (float)obs_data_get_double(settings, "art_spin_rpm");
	art.pulse_percent = (float)obs_data_get_double(settings, "art_pulse_percent");
	art.pulse_rate = (float)obs_data_get_double(settings, "art_pulse_rate");

	const char *placeholder = obs_data_get_string(settings, "art_placeholder");
	const std::string next_placeholder = placeholder ? placeholder : "";

	if (next_placeholder != art.placeholder) {
		art.placeholder = next_placeholder;
		widget->art_settings_dirty = true;
	}

	/* Text elements. */
	for (int slot = 0; slot < TEXT_SLOTS; slot++) {
		TextElement &element = widget->text[slot];
		const std::string prefix = text_prefix(slot);

		read_geometry(element.geometry, settings, prefix, true);

		const char *format = obs_data_get_string(settings, key_for(prefix, "format").c_str());
		const char *idle_format = obs_data_get_string(settings, key_for(prefix, "idle_format").c_str());

		element.format = format ? format : "";
		element.idle_format = idle_format ? idle_format : "";
		element.hide_empty = obs_data_get_bool(settings, key_for(prefix, "hide_empty").c_str());
		element.follow_accent = obs_data_get_bool(settings, key_for(prefix, "accent").c_str());
		element.rotation = (float)obs_data_get_double(settings, key_for(prefix, "rotation").c_str());

		read_text_style(element.style, settings, prefix);
	}

	/* Progress bar. */
	read_geometry(widget->bar_geometry, settings, "bar_", true);
	widget->bar.update(widget->self, settings, "bar_");

	/* The backdrop blur depends on the canvas size and on its own
	 * settings, so any of them changing means asking for it again. */
	widget->art_settings_dirty = true;
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

const char *widget_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MusicWidget");
}

uint32_t widget_get_width(void *data)
{
	return static_cast<MusicWidget *>(data)->width;
}

uint32_t widget_get_height(void *data)
{
	return static_cast<MusicWidget *>(data)->height;
}

void *widget_create(obs_data_t *settings, obs_source_t *source)
{
	MusicWidget *widget = new MusicWidget();
	widget->self = source;

	char *effect_path = obs_module_file("effects/music-widget.effect");

	obs_enter_graphics();
	widget->effect = gs_effect_create_from_file(effect_path, nullptr);
	widget->composite = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();

	bfree(effect_path);

	if (!widget->effect)
		obs_log(LOG_ERROR, "failed to load effects/music-widget.effect");

	widget->bar.create();
	widget->loader.start();

	widget_update(widget, settings);

	return widget;
}

void widget_destroy(void *data)
{
	MusicWidget *widget = static_cast<MusicWidget *>(data);

	widget->loader.stop();
	widget->bar.destroy();
	widget->link.release();

	obs_enter_graphics();

	for (TextElement &element : widget->text) {
		if (element.texture)
			gs_texture_destroy(element.texture);
	}

	if (widget->art_texture)
		gs_texture_destroy(widget->art_texture);

	if (widget->blur_texture)
		gs_texture_destroy(widget->blur_texture);

	if (widget->previous)
		gs_texture_destroy(widget->previous);

	if (widget->background.image_valid)
		gs_image_file4_free(&widget->background.image);

	if (widget->composite)
		gs_texrender_destroy(widget->composite);

	if (widget->effect)
		gs_effect_destroy(widget->effect);

	obs_leave_graphics();

	delete widget;
}

/* ------------------------------------------------------------------------- */
/* Tick                                                                       */
/* ------------------------------------------------------------------------- */

/* Whether the widget should be showing its "nothing playing" behaviour, decided
 * from the live playback state rather than from whatever the widget has chosen
 * to keep on screen. */
bool resolve_idle(const MusicWidget *widget)
{
	if (!widget->have_snapshot)
		return true;

	if (widget->snapshot.state == PlayState::Stopped)
		return true;

	return widget->idle_trigger == WidgetIdleTrigger::StoppedOrPaused &&
	       widget->snapshot.state == PlayState::Paused;
}

/* The snapshot the templates and the bar should be looking at, which is not
 * the live one when nothing is playing and the widget is set to freeze. */
const Snapshot *effective_snapshot(const MusicWidget *widget)
{
	if (!widget->idle)
		return widget->have_snapshot ? &widget->snapshot : nullptr;

	if (widget->idle_mode == WidgetIdle::Freeze && widget->have_frozen)
		return &widget->frozen;

	return widget->have_snapshot ? &widget->snapshot : nullptr;
}

std::string expand_for(const MusicWidget *widget, const TextElement &element)
{
	const Snapshot *snapshot = effective_snapshot(widget);

	TemplateContext context;
	context.snapshot = snapshot;
	context.have_snapshot = snapshot != nullptr;
	context.time_format = widget->time_format;
	context.state_playing = obs_module_text("State.Playing");
	context.state_paused = obs_module_text("State.Paused");
	context.state_stopped = obs_module_text("State.Stopped");

	/*
	 * Idle draws the idle template and nothing else, so a line left blank
	 * for this state really is blank rather than quietly carrying on with
	 * the last track's title. Freezing is the exception: keeping the last
	 * track on screen is the whole point of it, so there the playing
	 * template still applies unless an idle one has been written.
	 */
	const std::string *format = &element.format;

	if (widget->idle) {
		if (widget->idle_mode != WidgetIdle::Freeze || !element.idle_format.empty())
			format = &element.idle_format;
	}

	if (format->empty())
		return std::string();

	return expand_template(*format, context);
}

void refresh_text(MusicWidget *widget, TextElement &element)
{
	const std::string next = expand_for(widget, element);

	TextStyle style = element.style;
	style.color = resolve_color(widget, style.color, element.follow_accent);

	const uint32_t box_width = (uint32_t)std::max(1.0f, element.geometry.width);
	const uint32_t box_height = (uint32_t)std::max(1.0f, element.geometry.height);

	const bool unchanged = next == element.raster_text && style == element.raster_style &&
			       box_width == element.raster_width && box_height == element.raster_height;

	element.current = next;

	if (unchanged)
		return;

	element.raster_text = next;
	element.raster_style = style;
	element.raster_width = box_width;
	element.raster_height = box_height;
	element.scroll_time = 0.0f;

	if (!rasterize_text(next, style, box_width, box_height, element.raster))
		element.raster = RasterizedText();

	element.needs_upload = true;
}

void collect_art_result(MusicWidget *widget)
{
	ArtLoader::Result result;
	if (!widget->loader.take(result))
		return;

	widget->accent = result.accent;
	widget->accent_valid = result.accent_valid;

	if (result.have_art) {
		widget->pending_art = std::move(result.art);
		widget->upload_art = true;
		widget->clear_art = false;
	} else {
		widget->clear_art = true;
		widget->upload_art = false;
	}

	if (result.have_blur) {
		widget->pending_blur = std::move(result.blur);
		widget->upload_blur = true;
	}
}

void request_art(MusicWidget *widget)
{
	const Snapshot *snapshot = effective_snapshot(widget);

	/* Showing the placeholder means letting go of the last track's cover,
	 * so the loader is pointed at nothing and falls through to it. */
	const bool use_placeholder_only = widget->idle && widget->idle_mode == WidgetIdle::Placeholder;

	const std::string path = (snapshot && !use_placeholder_only) ? snapshot->path : std::string();

	const bool want_blur = widget->background.enabled && widget->background.type == BackgroundType::BlurredArt;

	if (!widget->art_settings_dirty && path == widget->requested_path &&
	    widget->art.placeholder == widget->requested_placeholder)
		return;

	widget->requested_path = path;
	widget->requested_placeholder = widget->art.placeholder;
	widget->art_settings_dirty = false;

	ArtLoader::Request request;
	request.path = path;
	request.placeholder = widget->art.placeholder;
	request.want_blur = want_blur;
	request.blur_width = widget->width;
	request.blur_height = widget->height;
	request.blur_strength = widget->background.blur / 100.0f;
	request.blur_darken = widget->background.darken / 100.0f;
	request.blur_desaturate = widget->background.desaturate / 100.0f;

	widget->loader.request(request);
}

void widget_tick(void *data, float seconds)
{
	MusicWidget *widget = static_cast<MusicWidget *>(data);

	/* Playback state. The link keeps looking for its music source, so a
	 * widget loaded before the source it points at still finds it. */
	widget->link.tick(seconds);

	widget->have_snapshot = false;

	if (obs_source_t *source = widget->link.get()) {
		widget->have_snapshot = get_snapshot(source, widget->snapshot);
		obs_source_release(source);
	}

	if (widget->have_snapshot && widget->snapshot.state != PlayState::Stopped) {
		widget->frozen = widget->snapshot;
		widget->have_frozen = true;
	}

	/* Decided before anything reads it: what the widget shows while idle is
	 * chosen from the live state, not from the snapshot it settles on. */
	widget->idle = resolve_idle(widget);

	const Snapshot *snapshot = effective_snapshot(widget);
	const bool playing = !widget->idle;

	/* Track changes drive both the transition and a fresh art request. */
	if (snapshot) {
		if (!widget->have_serial) {
			widget->have_serial = true;
			widget->last_serial = snapshot->track_serial;
		} else if (snapshot->track_serial != widget->last_serial) {
			widget->last_serial = snapshot->track_serial;

			if (widget->transition != TrackTransition::None && widget->transition_seconds > 0.0f &&
			    widget->previous_valid) {
				widget->transitioning = true;
				widget->transition_time = 0.0f;
			}
		}
	}

	if (widget->transitioning) {
		widget->transition_time += seconds;

		if (widget->transition_time >= widget->transition_seconds)
			widget->transitioning = false;
	}

	request_art(widget);
	collect_art_result(widget);

	/* Textures are only created here, on the graphics thread. */
	if (widget->clear_art) {
		destroy_texture(widget->art_texture);
		widget->clear_art = false;
	}

	if (widget->upload_art) {
		destroy_texture(widget->art_texture);
		widget->art_texture = create_texture(widget->pending_art);
		widget->pending_art.clear();
		widget->upload_art = false;
	}

	if (widget->upload_blur) {
		destroy_texture(widget->blur_texture);
		widget->blur_texture = create_texture(widget->pending_blur);
		widget->pending_blur.clear();
		widget->upload_blur = false;
	}

	widget->bar.set_accent(widget->accent, widget->accent_valid);
	widget->bar.tick(seconds, snapshot);

	/* Art animation. */
	ArtElement &art = widget->art;

	if (art.animation == ArtAnimation::Spin && playing) {
		art.spin_angle += seconds * art.spin_rpm * 6.0f; /* rpm to degrees per second */
		art.spin_angle = std::fmod(art.spin_angle, 360.0f);
	}

	if (art.animation == ArtAnimation::Pulse && playing)
		art.pulse_time += seconds;

	/* Text. */
	for (TextElement &element : widget->text) {
		if (!element.geometry.enabled)
			continue;

		refresh_text(widget, element);

		if (element.needs_upload) {
			destroy_texture(element.texture);
			element.texture = create_texture(element.raster.image);
			element.raster.image.clear();
			element.needs_upload = false;
		}

		if (element.raster.scrolls)
			element.scroll_time += seconds;
	}
}

/* ------------------------------------------------------------------------- */
/* Render                                                                     */
/* ------------------------------------------------------------------------- */

void draw_background(MusicWidget *widget)
{
	const BackgroundElement &background = widget->background;

	if (!background.enabled)
		return;

	const float inset = std::max(background.inset, 0.0f);
	const float width = (float)widget->width - inset * 2.0f;
	const float height = (float)widget->height - inset * 2.0f;

	if (width <= 0.0f || height <= 0.0f)
		return;

	DrawParams params;
	params.x = inset;
	params.y = inset;
	params.width = width;
	params.height = height;
	params.corner_radius = background.corner_radius;
	params.border_width = background.border_width;
	params.border_color = resolve_color(widget, background.border_color, background.border_accent);
	params.opacity = background.opacity;
	params.color = resolve_color(widget, background.color, background.follow_accent);
	params.color2 = background.color2;
	params.gradient_angle = background.gradient_angle;

	switch (background.type) {
	case BackgroundType::Gradient:
		params.fill_mode = 1;
		break;

	case BackgroundType::Image:
		if (background.image_valid) {
			params.fill_mode = 2;
			params.texture = background.image.image3.image2.image.texture;
			params.color = 0xFFFFFFFF;
		}
		break;

	case BackgroundType::BlurredArt:
		if (widget->blur_texture) {
			params.fill_mode = 2;
			params.texture = widget->blur_texture;
			params.color = 0xFFFFFFFF;
		}
		break;

	default:
		break;
	}

	draw_quad(widget->effect, params, false);
}

void draw_art(MusicWidget *widget)
{
	const ArtElement &art = widget->art;

	if (!art.geometry.enabled || !widget->art_texture)
		return;

	float x;
	float y;
	resolve_position(art.geometry, widget->width, widget->height, x, y);

	float width = art.geometry.width;
	float height = art.geometry.height;

	DrawParams params;
	params.fill_mode = 2;
	params.texture = widget->art_texture;
	params.corner_radius = art.corner_radius;
	params.border_width = art.border_width;
	params.border_color = resolve_color(widget, art.border_color, art.border_accent);
	params.opacity = art.geometry.opacity;
	params.color = 0xFFFFFFFF;

	const float texture_width = (float)gs_texture_get_width(widget->art_texture);
	const float texture_height = (float)gs_texture_get_height(widget->art_texture);

	if (texture_width > 0.0f && texture_height > 0.0f) {
		const float source_aspect = texture_width / texture_height;
		const float box_aspect = width / height;

		if (art.fit == ArtFit::Cover) {
			/* Crop in UV space so the box stays exactly as placed. */
			if (source_aspect > box_aspect) {
				params.uv_scale_x = box_aspect / source_aspect;
				params.uv_offset_x = (1.0f - params.uv_scale_x) * 0.5f;
			} else if (source_aspect < box_aspect) {
				params.uv_scale_y = source_aspect / box_aspect;
				params.uv_offset_y = (1.0f - params.uv_scale_y) * 0.5f;
			}
		} else if (art.fit == ArtFit::Contain) {
			/* Shrink the quad instead, so the letterboxed area is
			 * genuinely empty rather than smeared edge pixels. */
			if (source_aspect > box_aspect) {
				const float fitted = width / source_aspect;
				y += (height - fitted) * 0.5f;
				height = fitted;
			} else if (source_aspect < box_aspect) {
				const float fitted = height * source_aspect;
				x += (width - fitted) * 0.5f;
				width = fitted;
			}
		}
	}

	params.x = x;
	params.y = y;
	params.width = width;
	params.height = height;

	if (art.animation == ArtAnimation::Spin)
		params.rotation = art.rotation + art.spin_angle;
	else
		params.rotation = art.rotation;

	if (art.animation == ArtAnimation::Pulse) {
		const float phase = art.pulse_time * art.pulse_rate * 2.0f * PI;
		params.scale = 1.0f + std::sin(phase) * art.pulse_percent / 100.0f;
	}

	if (art.shadow) {
		DrawParams shadow;
		shadow.x = x + art.shadow_x;
		shadow.y = y + art.shadow_y;
		shadow.width = width;
		shadow.height = height;
		shadow.corner_radius = art.corner_radius;
		shadow.softness = art.shadow_blur;
		shadow.color = art.shadow_color;
		shadow.opacity = art.geometry.opacity;
		shadow.rotation = params.rotation;
		shadow.scale = params.scale;

		draw_quad(widget->effect, shadow, false);
	}

	draw_quad(widget->effect, params, false);
}

void draw_text(MusicWidget *widget, TextElement &element)
{
	if (!element.geometry.enabled || !element.texture)
		return;

	if (element.hide_empty && element.current.empty())
		return;

	float x;
	float y;
	resolve_position(element.geometry, widget->width, widget->height, x, y);

	/* The pixel buffer is released once uploaded, so the texture itself is
	 * the only remaining record of how big the raster came out. */
	const float full_width = (float)gs_texture_get_width(element.texture);
	const float texture_height = (float)gs_texture_get_height(element.texture);

	DrawParams params;
	params.fill_mode = 2;
	params.texture = element.texture;
	params.opacity = element.geometry.opacity;
	params.rotation = element.rotation;
	params.color = 0xFFFFFFFF;

	if (element.raster.scrolls && full_width > 0.0f) {
		const float offset = marquee_offset(element.raster, element.raster_style,
						    (uint32_t)element.geometry.width, element.scroll_time);

		params.x = x;
		params.y = y - (float)element.raster.pad;
		params.width = element.geometry.width;
		params.height = texture_height;
		params.uv_scale_x = element.geometry.width / full_width;
		params.uv_offset_x = offset / full_width;
	} else {
		params.x = x - (float)element.raster.pad;
		params.y = y - (float)element.raster.pad;
		params.width = full_width;
		params.height = texture_height;
	}

	draw_quad(widget->effect, params, true);
}

void draw_bar(MusicWidget *widget)
{
	if (!widget->bar_geometry.enabled || widget->bar.hidden())
		return;

	float x;
	float y;
	resolve_position(widget->bar_geometry, widget->width, widget->height, x, y);

	const uint32_t width = (uint32_t)std::max(1.0f, widget->bar_geometry.width);
	const uint32_t height = (uint32_t)std::max(1.0f, widget->bar_geometry.height);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);

	widget->bar.render(width, height);

	gs_matrix_pop();
}

/* Everything the widget draws, in the order the z values ask for. */
void draw_elements(MusicWidget *widget)
{
	struct Item {
		int z;
		int order;
		int kind; /* 0 background, 1 art, 2 bar, 3..8 text slots */
	};

	std::vector<Item> items;
	items.reserve(TEXT_SLOTS + 3);

	int order = 0;

	if (widget->background.enabled)
		items.push_back({widget->background.z, order++, 0});

	if (widget->art.geometry.enabled)
		items.push_back({widget->art.geometry.z, order++, 1});

	if (widget->bar_geometry.enabled)
		items.push_back({widget->bar_geometry.z, order++, 2});

	for (int slot = 0; slot < TEXT_SLOTS; slot++) {
		if (widget->text[slot].geometry.enabled)
			items.push_back({widget->text[slot].geometry.z, order++, 3 + slot});
	}

	/* Equal z values keep their declaration order, so the panel is behind
	 * the art is behind the text unless told otherwise. */
	std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
		if (a.z != b.z)
			return a.z < b.z;
		return a.order < b.order;
	});

	for (const Item &item : items) {
		switch (item.kind) {
		case 0:
			draw_background(widget);
			break;
		case 1:
			draw_art(widget);
			break;
		case 2:
			draw_bar(widget);
			break;
		default:
			draw_text(widget, widget->text[item.kind - 3]);
			break;
		}
	}
}

void ensure_previous_texture(MusicWidget *widget)
{
	if (widget->previous && widget->previous_width == widget->width && widget->previous_height == widget->height)
		return;

	if (widget->previous) {
		gs_texture_destroy(widget->previous);
		widget->previous = nullptr;
	}

	widget->previous = gs_texture_create(widget->width, widget->height, GS_RGBA, 1, nullptr, GS_RENDER_TARGET);
	widget->previous_width = widget->width;
	widget->previous_height = widget->height;
	widget->previous_valid = false;
}

void widget_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);

	MusicWidget *widget = static_cast<MusicWidget *>(data);

	if (!widget->effect || !widget->composite)
		return;

	if (widget->idle && widget->idle_mode == WidgetIdle::Hide)
		return;

	/* Source backed bar layers render into targets of their own, which has
	 * to happen before this source opens one. */
	widget->bar.prepare((uint32_t)std::max(1.0f, widget->bar_geometry.width),
			    (uint32_t)std::max(1.0f, widget->bar_geometry.height));

	gs_texrender_reset(widget->composite);

	/*
	 * draw_quad hands the shader colours and textures already converted to
	 * linear whenever OBS is working that way, so the framebuffer has to be
	 * told to encode back to sRGB on the way out. Without it every colour
	 * lands darker and more saturated than it was picked - a pink panel
	 * arriving on screen as red - and the card looks like it has a shadow
	 * cast over it.
	 */
	const bool linear_srgb = gs_get_linear_srgb();
	const bool previous_srgb = gs_framebuffer_srgb_enabled();

	gs_enable_framebuffer_srgb(linear_srgb);

	gs_blend_state_push();

	/* Colour blends normally, but alpha accumulates rather than being
	 * replaced, so stacking translucent elements into an empty target
	 * leaves the card's own edges correctly transparent. */
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	if (gs_texrender_begin(widget->composite, widget->width, widget->height)) {
		/* Binding a render target clears the flag, so it is set again
		 * for the composite pass and once more for the blit below. */
		gs_enable_framebuffer_srgb(linear_srgb);

		struct vec4 clear_color;
		vec4_zero(&clear_color);

		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)widget->width, 0.0f, (float)widget->height, -100.0f, 100.0f);

		draw_elements(widget);

		gs_texrender_end(widget->composite);
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(linear_srgb);

	gs_texture_t *current = gs_texrender_get_texture(widget->composite);
	if (!current) {
		gs_enable_framebuffer_srgb(previous_srgb);
		return;
	}

	ensure_previous_texture(widget);

	const bool transitioning = widget->transitioning && widget->previous_valid && widget->transition_seconds > 0.0f;

	float progress = 1.0f;
	if (transitioning)
		progress = std::min(widget->transition_time / widget->transition_seconds, 1.0f);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	const auto blit = [&](gs_texture_t *texture, float opacity, float offset_x) {
		DrawParams params;
		params.fill_mode = 2;
		params.texture = texture;
		params.x = offset_x;
		params.y = 0.0f;
		params.width = (float)widget->width;
		params.height = (float)widget->height;
		params.opacity = opacity;
		params.color = 0xFFFFFFFF;

		draw_quad(widget->effect, params, true);
	};

	if (!transitioning) {
		blit(current, 1.0f, 0.0f);
	} else {
		switch (widget->transition) {
		case TrackTransition::Slide: {
			const float travel = (float)widget->width;
			blit(widget->previous, 1.0f, -travel * progress);
			blit(current, 1.0f, travel * (1.0f - progress));
			break;
		}

		case TrackTransition::FadeThroughBlank:
			if (progress < 0.5f)
				blit(widget->previous, 1.0f - progress * 2.0f, 0.0f);
			else
				blit(current, (progress - 0.5f) * 2.0f, 0.0f);
			break;

		default:
			blit(widget->previous, 1.0f - progress, 0.0f);
			blit(current, progress, 0.0f);
			break;
		}
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);

	/* Hold on to a clean frame so the next track change has something to
	 * fade away from. */
	if (!transitioning && widget->previous) {
		gs_copy_texture(widget->previous, current);
		widget->previous_valid = true;
	}
}

void widget_enum_active_sources(void *data, obs_source_enum_proc_t callback, void *param)
{
	MusicWidget *widget = static_cast<MusicWidget *>(data);
	widget->bar.enum_active_sources(widget->self, callback, param);
}

/* ------------------------------------------------------------------------- */
/* Defaults                                                                   */
/* ------------------------------------------------------------------------- */

void set_geometry_defaults(obs_data_t *settings, const std::string &prefix, bool enabled, Anchor anchor, double x,
			   double y, int64_t width, int64_t height, int64_t z)
{
	obs_data_set_default_bool(settings, key_for(prefix, "enable").c_str(), enabled);
	obs_data_set_default_int(settings, key_for(prefix, "anchor").c_str(), (int64_t)anchor);
	obs_data_set_default_double(settings, key_for(prefix, "x").c_str(), x);
	obs_data_set_default_double(settings, key_for(prefix, "y").c_str(), y);
	obs_data_set_default_int(settings, key_for(prefix, "width").c_str(), width);
	obs_data_set_default_int(settings, key_for(prefix, "height").c_str(), height);
	obs_data_set_default_double(settings, key_for(prefix, "opacity").c_str(), 100.0);
	obs_data_set_default_int(settings, key_for(prefix, "z").c_str(), z);
}

void set_font_default(obs_data_t *settings, const std::string &prefix, const char *face, int64_t size, bool bold)
{
	obs_data_t *font = obs_data_create();

	obs_data_set_string(font, "face", face);
	obs_data_set_string(font, "style", bold ? "Bold" : "Regular");
	obs_data_set_int(font, "size", size);
	obs_data_set_int(font, "flags", bold ? OBS_FONT_BOLD : 0);

	obs_data_set_default_obj(settings, key_for(prefix, "font").c_str(), font);

	obs_data_release(font);
}

void set_text_defaults(obs_data_t *settings, int slot)
{
	const std::string prefix = text_prefix(slot);

	/* Only the first two slots are switched on out of the box: a title and
	 * an artist is what a fresh widget should look like. */
	const bool enabled = slot < 2;

	obs_data_set_default_string(settings, key_for(prefix, "format").c_str(),
				    slot == 0 ? "{title}" : (slot == 1 ? "{artist}" : ""));
	obs_data_set_default_string(settings, key_for(prefix, "idle_format").c_str(), slot == 0 ? "{state}" : "");
	obs_data_set_default_bool(settings, key_for(prefix, "hide_empty").c_str(), true);
	obs_data_set_default_bool(settings, key_for(prefix, "accent").c_str(), false);
	obs_data_set_default_double(settings, key_for(prefix, "rotation").c_str(), 0.0);

	set_geometry_defaults(settings, prefix, enabled, Anchor::TopLeft, 200.0, slot == 0 ? 34.0 : 76.0, 410, 40, 10);

	set_font_default(settings, prefix, "Arial", slot == 0 ? 30 : 22, slot == 0);

	obs_data_set_default_int(settings, key_for(prefix, "color").c_str(), slot == 0 ? 0xFFFFFFFF : 0xFFC8C8C8);
	obs_data_set_default_int(settings, key_for(prefix, "color2").c_str(), 0xFF9B5CFF);
	obs_data_set_default_bool(settings, key_for(prefix, "gradient").c_str(), false);
	obs_data_set_default_double(settings, key_for(prefix, "gradient_angle").c_str(), 90.0);

	obs_data_set_default_double(settings, key_for(prefix, "outline").c_str(), 0.0);
	obs_data_set_default_int(settings, key_for(prefix, "outline_color").c_str(), 0xFF000000);

	obs_data_set_default_bool(settings, key_for(prefix, "shadow").c_str(), false);
	obs_data_set_default_double(settings, key_for(prefix, "shadow_x").c_str(), 2.0);
	obs_data_set_default_double(settings, key_for(prefix, "shadow_y").c_str(), 2.0);
	obs_data_set_default_double(settings, key_for(prefix, "shadow_blur").c_str(), 4.0);
	obs_data_set_default_int(settings, key_for(prefix, "shadow_color").c_str(), 0xC0000000);

	obs_data_set_default_double(settings, key_for(prefix, "letter_spacing").c_str(), 0.0);
	obs_data_set_default_double(settings, key_for(prefix, "line_height").c_str(), 100.0);

	obs_data_set_default_int(settings, key_for(prefix, "transform").c_str(), (int64_t)TextTransform::None);
	obs_data_set_default_int(settings, key_for(prefix, "align").c_str(), (int64_t)TextAlign::Left);
	obs_data_set_default_int(settings, key_for(prefix, "valign").c_str(), (int64_t)TextVAlign::Middle);

	obs_data_set_default_int(settings, key_for(prefix, "overflow").c_str(), (int64_t)TextOverflow::Marquee);
	obs_data_set_default_int(settings, key_for(prefix, "marquee_style").c_str(), (int64_t)MarqueeStyle::Loop);
	obs_data_set_default_int(settings, key_for(prefix, "ellipsis_side").c_str(), (int64_t)EllipsisSide::Right);
	obs_data_set_default_int(settings, key_for(prefix, "max_lines").c_str(), 2);
	obs_data_set_default_double(settings, key_for(prefix, "shrink_min").c_str(), 50.0);
	obs_data_set_default_double(settings, key_for(prefix, "marquee_gap").c_str(), 64.0);
	obs_data_set_default_double(settings, key_for(prefix, "marquee_speed").c_str(), 60.0);
	obs_data_set_default_double(settings, key_for(prefix, "marquee_pause").c_str(), 1.0);
}

void widget_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 640);
	obs_data_set_default_int(settings, "height", 180);
	obs_data_set_default_int(settings, "time_format", (int64_t)TimeFormat::Auto);
	obs_data_set_default_int(settings, "transition", (int64_t)TrackTransition::CrossFade);
	obs_data_set_default_int(settings, "transition_ms", 350);
	obs_data_set_default_int(settings, "idle_mode", (int64_t)WidgetIdle::Placeholder);
	obs_data_set_default_int(settings, "idle_trigger", (int64_t)WidgetIdleTrigger::Stopped);
	obs_data_set_default_int(settings, "preset", (int64_t)LayoutPreset::ArtLeft);

	obs_data_set_default_bool(settings, "bg_enable", true);
	obs_data_set_default_int(settings, "bg_type", (int64_t)BackgroundType::Color);
	obs_data_set_default_int(settings, "bg_color", 0xC0141414);
	obs_data_set_default_int(settings, "bg_color2", 0xC0303030);
	obs_data_set_default_double(settings, "bg_gradient_angle", 90.0);
	obs_data_set_default_bool(settings, "bg_accent", false);
	obs_data_set_default_double(settings, "bg_blur", 70.0);
	obs_data_set_default_double(settings, "bg_darken", 45.0);
	obs_data_set_default_double(settings, "bg_desaturate", 20.0);
	obs_data_set_default_double(settings, "bg_inset", 0.0);
	obs_data_set_default_double(settings, "bg_corner_radius", 12.0);
	obs_data_set_default_double(settings, "bg_border_width", 0.0);
	obs_data_set_default_int(settings, "bg_border_color", 0xFF000000);
	obs_data_set_default_bool(settings, "bg_border_accent", false);
	obs_data_set_default_double(settings, "bg_opacity", 100.0);
	obs_data_set_default_int(settings, "bg_z", -100);

	set_geometry_defaults(settings, "art_", true, Anchor::MiddleLeft, 16.0, 0.0, 148, 148, 0);
	obs_data_set_default_int(settings, "art_fit", (int64_t)ArtFit::Cover);
	obs_data_set_default_double(settings, "art_corner_radius", 8.0);
	obs_data_set_default_double(settings, "art_border_width", 0.0);
	obs_data_set_default_int(settings, "art_border_color", 0xFF000000);
	obs_data_set_default_bool(settings, "art_border_accent", false);
	obs_data_set_default_bool(settings, "art_shadow", true);
	obs_data_set_default_double(settings, "art_shadow_x", 0.0);
	obs_data_set_default_double(settings, "art_shadow_y", 6.0);
	obs_data_set_default_double(settings, "art_shadow_blur", 12.0);
	obs_data_set_default_int(settings, "art_shadow_color", 0xA0000000);
	obs_data_set_default_int(settings, "art_animation", (int64_t)ArtAnimation::None);
	obs_data_set_default_double(settings, "art_rotation", 0.0);
	obs_data_set_default_double(settings, "art_spin_rpm", 12.0);
	obs_data_set_default_double(settings, "art_pulse_percent", 4.0);
	obs_data_set_default_double(settings, "art_pulse_rate", 1.0);

	for (int slot = 0; slot < TEXT_SLOTS; slot++)
		set_text_defaults(settings, slot);

	/* The bar's own defaults assume it owns the canvas, so the element
	 * geometry is written afterwards and wins on size and position. */
	BarRenderer::add_defaults(settings, "bar_");
	set_geometry_defaults(settings, "bar_", true, Anchor::BottomLeft, 200.0, -26.0, 410, 10, 20);

	obs_data_set_default_double(settings, "bar_corner_radius", 5.0);
	obs_data_set_default_int(settings, "bar_bg_color", 0x60FFFFFF);
}

/* ------------------------------------------------------------------------- */
/* Presets                                                                    */
/* ------------------------------------------------------------------------- */

void write_geometry(obs_data_t *settings, const std::string &prefix, bool enabled, Anchor anchor, double x, double y,
		    int64_t width, int64_t height)
{
	obs_data_set_bool(settings, key_for(prefix, "enable").c_str(), enabled);
	obs_data_set_int(settings, key_for(prefix, "anchor").c_str(), (int64_t)anchor);
	obs_data_set_double(settings, key_for(prefix, "x").c_str(), x);
	obs_data_set_double(settings, key_for(prefix, "y").c_str(), y);
	obs_data_set_int(settings, key_for(prefix, "width").c_str(), width);
	obs_data_set_int(settings, key_for(prefix, "height").c_str(), height);
}

void write_font(obs_data_t *settings, const std::string &prefix, int64_t size, bool bold)
{
	obs_data_t *font = obs_data_get_obj(settings, key_for(prefix, "font").c_str());

	if (!font)
		font = obs_data_create();

	const char *face = obs_data_get_string(font, "face");
	if (!face || !*face)
		obs_data_set_string(font, "face", "Arial");

	obs_data_set_int(font, "size", size);
	obs_data_set_int(font, "flags", bold ? OBS_FONT_BOLD : 0);
	obs_data_set_string(font, "style", bold ? "Bold" : "Regular");

	obs_data_set_obj(settings, key_for(prefix, "font").c_str(), font);

	obs_data_release(font);
}

/*
 * Presets write real values into the same fields the user edits, so applying
 * one is a starting point and never a mode: everything stays adjustable
 * afterwards.
 */
void apply_preset(obs_data_t *settings, LayoutPreset preset)
{
	/* Slots past the two a preset uses are switched off rather than left
	 * showing whatever the last layout put there. */
	for (int slot = 2; slot < TEXT_SLOTS; slot++)
		obs_data_set_bool(settings, key_for(text_prefix(slot), "enable").c_str(), false);

	switch (preset) {
	case LayoutPreset::ArtRight:
		obs_data_set_int(settings, "width", 640);
		obs_data_set_int(settings, "height", 180);
		write_geometry(settings, "art_", true, Anchor::MiddleRight, -16.0, 0.0, 148, 148);
		write_geometry(settings, text_prefix(0), true, Anchor::TopLeft, 24.0, 34.0, 410, 40);
		write_geometry(settings, text_prefix(1), true, Anchor::TopLeft, 24.0, 76.0, 410, 32);
		write_geometry(settings, "bar_", true, Anchor::BottomLeft, 24.0, -26.0, 410, 10);
		write_font(settings, text_prefix(0), 30, true);
		write_font(settings, text_prefix(1), 22, false);
		break;

	case LayoutPreset::VerticalCard:
		obs_data_set_int(settings, "width", 400);
		obs_data_set_int(settings, "height", 480);
		write_geometry(settings, "art_", true, Anchor::TopCenter, 0.0, 24.0, 320, 320);
		write_geometry(settings, text_prefix(0), true, Anchor::TopCenter, 0.0, 360.0, 352, 44);
		write_geometry(settings, text_prefix(1), true, Anchor::TopCenter, 0.0, 404.0, 352, 32);
		write_geometry(settings, "bar_", true, Anchor::BottomCenter, 0.0, -22.0, 352, 8);
		write_font(settings, text_prefix(0), 32, true);
		write_font(settings, text_prefix(1), 22, false);
		obs_data_set_int(settings, key_for(text_prefix(0), "align").c_str(), (int64_t)TextAlign::Center);
		obs_data_set_int(settings, key_for(text_prefix(1), "align").c_str(), (int64_t)TextAlign::Center);
		break;

	case LayoutPreset::WideBanner:
		obs_data_set_int(settings, "width", 800);
		obs_data_set_int(settings, "height", 120);
		write_geometry(settings, "art_", true, Anchor::MiddleLeft, 12.0, 0.0, 96, 96);
		write_geometry(settings, text_prefix(0), true, Anchor::TopLeft, 124.0, 22.0, 660, 36);
		write_geometry(settings, text_prefix(1), true, Anchor::TopLeft, 124.0, 58.0, 660, 28);
		write_geometry(settings, "bar_", true, Anchor::BottomLeft, 124.0, -18.0, 660, 6);
		write_font(settings, text_prefix(0), 26, true);
		write_font(settings, text_prefix(1), 20, false);
		break;

	case LayoutPreset::CompactBar:
		obs_data_set_int(settings, "width", 420);
		obs_data_set_int(settings, "height", 64);
		write_geometry(settings, "art_", true, Anchor::MiddleLeft, 8.0, 0.0, 48, 48);
		write_geometry(settings, text_prefix(0), true, Anchor::TopLeft, 66.0, 10.0, 344, 24);
		write_geometry(settings, text_prefix(1), true, Anchor::TopLeft, 66.0, 32.0, 344, 18);
		write_geometry(settings, "bar_", true, Anchor::BottomLeft, 66.0, -8.0, 344, 4);
		write_font(settings, text_prefix(0), 18, true);
		write_font(settings, text_prefix(1), 14, false);
		break;

	case LayoutPreset::TextOnly:
		obs_data_set_int(settings, "width", 560);
		obs_data_set_int(settings, "height", 110);
		write_geometry(settings, "art_", false, Anchor::MiddleLeft, 16.0, 0.0, 148, 148);
		write_geometry(settings, text_prefix(0), true, Anchor::TopLeft, 20.0, 16.0, 520, 40);
		write_geometry(settings, text_prefix(1), true, Anchor::TopLeft, 20.0, 56.0, 520, 30);
		write_geometry(settings, "bar_", true, Anchor::BottomLeft, 20.0, -18.0, 520, 8);
		write_font(settings, text_prefix(0), 30, true);
		write_font(settings, text_prefix(1), 22, false);
		break;

	default:
		obs_data_set_int(settings, "width", 640);
		obs_data_set_int(settings, "height", 180);
		write_geometry(settings, "art_", true, Anchor::MiddleLeft, 16.0, 0.0, 148, 148);
		write_geometry(settings, text_prefix(0), true, Anchor::TopLeft, 200.0, 34.0, 410, 40);
		write_geometry(settings, text_prefix(1), true, Anchor::TopLeft, 200.0, 76.0, 410, 32);
		write_geometry(settings, "bar_", true, Anchor::BottomLeft, 200.0, -26.0, 410, 10);
		write_font(settings, text_prefix(0), 30, true);
		write_font(settings, text_prefix(1), 22, false);
		break;
	}
}

bool preset_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	MusicWidget *widget = static_cast<MusicWidget *>(data);
	if (!widget || !widget->self)
		return false;

	obs_data_t *settings = obs_source_get_settings(widget->self);
	if (!settings)
		return false;

	apply_preset(settings, (LayoutPreset)obs_data_get_int(settings, "preset"));

	obs_source_update(widget->self, settings);
	obs_data_release(settings);

	/* Redraws the whole panel so the new values show up in the fields. */
	return true;
}

/* ------------------------------------------------------------------------- */
/* Properties                                                                 */
/* ------------------------------------------------------------------------- */

void add_anchor_list(obs_properties_t *props, const std::string &prefix)
{
	obs_property_t *anchor = obs_properties_add_list(props, key_for(prefix, "anchor").c_str(),
							 obs_module_text("Widget.Anchor"), OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_INT);

	static const struct {
		const char *text;
		Anchor value;
	} ANCHORS[] = {
		{"Widget.Anchor.TopLeft", Anchor::TopLeft},
		{"Widget.Anchor.TopCenter", Anchor::TopCenter},
		{"Widget.Anchor.TopRight", Anchor::TopRight},
		{"Widget.Anchor.MiddleLeft", Anchor::MiddleLeft},
		{"Widget.Anchor.Center", Anchor::Center},
		{"Widget.Anchor.MiddleRight", Anchor::MiddleRight},
		{"Widget.Anchor.BottomLeft", Anchor::BottomLeft},
		{"Widget.Anchor.BottomCenter", Anchor::BottomCenter},
		{"Widget.Anchor.BottomRight", Anchor::BottomRight},
	};

	for (const auto &entry : ANCHORS)
		obs_property_list_add_int(anchor, obs_module_text(entry.text), (int64_t)entry.value);

	obs_property_set_long_description(anchor, obs_module_text("Widget.Anchor.Description"));
}

/* The bar has an opacity control on each of its own fill layers, so the
 * element level one would only be a second way to say the same thing. */
void add_geometry_properties(obs_properties_t *props, const std::string &prefix, bool include_opacity = true)
{
	add_anchor_list(props, prefix);

	obs_property_t *x = obs_properties_add_float(props, key_for(prefix, "x").c_str(),
						     obs_module_text("Widget.OffsetX"), -8192.0, 8192.0, 1.0);
	obs_property_float_set_suffix(x, " px");

	obs_property_t *y = obs_properties_add_float(props, key_for(prefix, "y").c_str(),
						     obs_module_text("Widget.OffsetY"), -8192.0, 8192.0, 1.0);
	obs_property_float_set_suffix(y, " px");

	obs_properties_add_int(props, key_for(prefix, "width").c_str(), obs_module_text("Widget.ElementWidth"), 1, 8192,
			       1);
	obs_properties_add_int(props, key_for(prefix, "height").c_str(), obs_module_text("Widget.ElementHeight"), 1,
			       8192, 1);

	if (include_opacity) {
		obs_property_t *opacity = obs_properties_add_float_slider(
			props, key_for(prefix, "opacity").c_str(), obs_module_text("Bar.Opacity"), 0.0, 100.0, 1.0);
		obs_property_float_set_suffix(opacity, "%");
	}

	obs_property_t *z = obs_properties_add_int(props, key_for(prefix, "z").c_str(), obs_module_text("Widget.Layer"),
						   -1000, 1000, 1);
	obs_property_set_long_description(z, obs_module_text("Widget.Layer.Description"));
}

/* Shows only the backdrop controls that the chosen fill needs. */
bool background_type_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	const BackgroundType type = (BackgroundType)obs_data_get_int(settings, "bg_type");

	const auto set_visible = [&](const char *name, bool visible) {
		obs_property_t *target = obs_properties_get(props, name);
		if (target)
			obs_property_set_visible(target, visible);
	};

	set_visible("bg_color", type != BackgroundType::Image && type != BackgroundType::BlurredArt);
	set_visible("bg_accent", type == BackgroundType::Color || type == BackgroundType::Gradient);
	set_visible("bg_color2", type == BackgroundType::Gradient);
	set_visible("bg_gradient_angle", type == BackgroundType::Gradient);
	set_visible("bg_image", type == BackgroundType::Image);
	set_visible("bg_blur", type == BackgroundType::BlurredArt);
	set_visible("bg_darken", type == BackgroundType::BlurredArt);
	set_visible("bg_desaturate", type == BackgroundType::BlurredArt);

	return true;
}

/* Shows only the overflow controls that the chosen mode needs. */
bool overflow_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	const std::string name = obs_property_name(property);
	if (name.size() < 8)
		return false;

	const std::string prefix = name.substr(0, name.size() - 8); /* drop "overflow" */
	const TextOverflow overflow = (TextOverflow)obs_data_get_int(settings, name.c_str());

	const auto set_visible = [&](const char *suffix, bool visible) {
		obs_property_t *target = obs_properties_get(props, key_for(prefix, suffix).c_str());
		if (target)
			obs_property_set_visible(target, visible);
	};

	set_visible("marquee_style", overflow == TextOverflow::Marquee);
	set_visible("marquee_speed", overflow == TextOverflow::Marquee);
	set_visible("marquee_gap", overflow == TextOverflow::Marquee);
	set_visible("marquee_pause", overflow == TextOverflow::Marquee);
	set_visible("ellipsis_side", overflow == TextOverflow::Ellipsis);
	set_visible("max_lines", overflow == TextOverflow::Wrap);
	set_visible("shrink_min", overflow == TextOverflow::Shrink);

	return true;
}

/* Shows only the art animation controls in use. */
bool art_animation_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	const ArtAnimation animation = (ArtAnimation)obs_data_get_int(settings, "art_animation");

	const auto set_visible = [&](const char *name, bool visible) {
		obs_property_t *target = obs_properties_get(props, name);
		if (target)
			obs_property_set_visible(target, visible);
	};

	set_visible("art_spin_rpm", animation == ArtAnimation::Spin);
	set_visible("art_pulse_percent", animation == ArtAnimation::Pulse);
	set_visible("art_pulse_rate", animation == ArtAnimation::Pulse);

	return true;
}

void add_background_properties(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();

	obs_property_t *type = obs_properties_add_list(group, "bg_type", obs_module_text("Widget.Background.Type"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Color"), (int64_t)BackgroundType::Color);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Gradient"), (int64_t)BackgroundType::Gradient);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Image"), (int64_t)BackgroundType::Image);
	obs_property_list_add_int(type, obs_module_text("Widget.Background.BlurredArt"),
				  (int64_t)BackgroundType::BlurredArt);
	obs_property_set_modified_callback(type, background_type_modified);

	obs_properties_add_color_alpha(group, "bg_color", obs_module_text("Bar.Color"));
	obs_properties_add_bool(group, "bg_accent", obs_module_text("Bar.FollowAccent"));
	obs_properties_add_color_alpha(group, "bg_color2", obs_module_text("Bar.Color2"));

	obs_property_t *angle = obs_properties_add_float_slider(group, "bg_gradient_angle",
								obs_module_text("Bar.GradientAngle"), 0.0, 360.0, 1.0);
	obs_property_float_set_suffix(angle, "°");

	obs_properties_add_path(group, "bg_image", obs_module_text("Bar.Image"), OBS_PATH_FILE,
				obs_module_text("Bar.Image.Filter"), nullptr);

	obs_property_t *blur = obs_properties_add_float_slider(
		group, "bg_blur", obs_module_text("Widget.Background.Blur"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(blur, "%");

	obs_property_t *darken = obs_properties_add_float_slider(
		group, "bg_darken", obs_module_text("Widget.Background.Darken"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(darken, "%");

	obs_property_t *desaturate = obs_properties_add_float_slider(
		group, "bg_desaturate", obs_module_text("Widget.Background.Desaturate"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(desaturate, "%");

	obs_property_t *inset = obs_properties_add_float(group, "bg_inset", obs_module_text("Widget.Background.Inset"),
							 0.0, 4096.0, 1.0);
	obs_property_float_set_suffix(inset, " px");

	obs_properties_add_float_slider(group, "bg_corner_radius", obs_module_text("Bar.CornerRadius"), 0.0, 512.0,
					1.0);
	obs_properties_add_float_slider(group, "bg_border_width", obs_module_text("Bar.BorderWidth"), 0.0, 64.0, 1.0);
	obs_properties_add_color_alpha(group, "bg_border_color", obs_module_text("Bar.BorderColor"));
	obs_properties_add_bool(group, "bg_border_accent", obs_module_text("Bar.BorderFollowAccent"));

	obs_property_t *opacity =
		obs_properties_add_float_slider(group, "bg_opacity", obs_module_text("Bar.Opacity"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(opacity, "%");

	obs_properties_add_int(group, "bg_z", obs_module_text("Widget.Layer"), -1000, 1000, 1);

	obs_properties_add_group(props, "bg_enable", obs_module_text("Widget.Background"), OBS_GROUP_CHECKABLE, group);
}

void add_art_properties(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();

	add_geometry_properties(group, "art_");

	obs_property_t *fit = obs_properties_add_list(group, "art_fit", obs_module_text("Widget.Art.Fit"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fit, obs_module_text("Widget.Art.Fit.Cover"), (int64_t)ArtFit::Cover);
	obs_property_list_add_int(fit, obs_module_text("Widget.Art.Fit.Contain"), (int64_t)ArtFit::Contain);
	obs_property_list_add_int(fit, obs_module_text("Widget.Art.Fit.Stretch"), (int64_t)ArtFit::Stretch);

	obs_properties_add_float_slider(group, "art_corner_radius", obs_module_text("Bar.CornerRadius"), 0.0, 512.0,
					1.0);
	obs_properties_add_float_slider(group, "art_border_width", obs_module_text("Bar.BorderWidth"), 0.0, 64.0, 1.0);
	obs_properties_add_color_alpha(group, "art_border_color", obs_module_text("Bar.BorderColor"));
	obs_properties_add_bool(group, "art_border_accent", obs_module_text("Bar.BorderFollowAccent"));

	obs_properties_add_bool(group, "art_shadow", obs_module_text("Widget.Shadow"));
	obs_properties_add_float(group, "art_shadow_x", obs_module_text("Widget.Shadow.X"), -256.0, 256.0, 1.0);
	obs_properties_add_float(group, "art_shadow_y", obs_module_text("Widget.Shadow.Y"), -256.0, 256.0, 1.0);
	obs_properties_add_float_slider(group, "art_shadow_blur", obs_module_text("Widget.Shadow.Blur"), 0.0, 128.0,
					1.0);
	obs_properties_add_color_alpha(group, "art_shadow_color", obs_module_text("Widget.Shadow.Color"));

	obs_property_t *rotation = obs_properties_add_float_slider(
		group, "art_rotation", obs_module_text("Widget.Rotation"), -180.0, 180.0, 1.0);
	obs_property_float_set_suffix(rotation, "°");

	obs_property_t *animation = obs_properties_add_list(group, "art_animation",
							    obs_module_text("Widget.Art.Animation"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(animation, obs_module_text("Widget.Art.Animation.None"), (int64_t)ArtAnimation::None);
	obs_property_list_add_int(animation, obs_module_text("Widget.Art.Animation.Spin"), (int64_t)ArtAnimation::Spin);
	obs_property_list_add_int(animation, obs_module_text("Widget.Art.Animation.Pulse"),
				  (int64_t)ArtAnimation::Pulse);
	obs_property_set_modified_callback(animation, art_animation_modified);
	obs_property_set_long_description(animation, obs_module_text("Widget.Art.Animation.Description"));

	obs_property_t *rpm = obs_properties_add_float_slider(group, "art_spin_rpm", obs_module_text("Widget.Art.Rpm"),
							      0.0, 120.0, 0.5);
	obs_property_float_set_suffix(rpm, " rpm");

	obs_property_t *pulse = obs_properties_add_float_slider(
		group, "art_pulse_percent", obs_module_text("Widget.Art.PulseAmount"), 0.0, 50.0, 0.5);
	obs_property_float_set_suffix(pulse, "%");

	obs_property_t *rate = obs_properties_add_float_slider(group, "art_pulse_rate",
							       obs_module_text("Widget.Art.PulseRate"), 0.1, 10.0, 0.1);
	obs_property_float_set_suffix(rate, " Hz");

	obs_property_t *placeholder = obs_properties_add_path(group, "art_placeholder",
							      obs_module_text("Widget.Art.Placeholder"), OBS_PATH_FILE,
							      obs_module_text("Bar.Image.Filter"), nullptr);
	obs_property_set_long_description(placeholder, obs_module_text("Widget.Art.Placeholder.Description"));

	obs_properties_add_group(props, "art_enable", obs_module_text("Widget.Art"), OBS_GROUP_CHECKABLE, group);
}

void add_text_properties(obs_properties_t *props, int slot)
{
	const std::string prefix = text_prefix(slot);

	obs_properties_t *group = obs_properties_create();

	obs_property_t *format = obs_properties_add_text(group, key_for(prefix, "format").c_str(),
							 obs_module_text("Info.Format"), OBS_TEXT_MULTILINE);
	obs_property_set_long_description(format, obs_module_text("Info.Fields"));

	obs_properties_add_text(group, key_for(prefix, "idle_format").c_str(), obs_module_text("Info.IdleFormat"),
				OBS_TEXT_MULTILINE);

	obs_property_t *hide_empty = obs_properties_add_bool(group, key_for(prefix, "hide_empty").c_str(),
							     obs_module_text("Widget.Text.HideEmpty"));
	obs_property_set_long_description(hide_empty, obs_module_text("Widget.Text.HideEmpty.Description"));

	add_geometry_properties(group, prefix);

	obs_property_t *font =
		obs_properties_add_font(group, key_for(prefix, "font").c_str(), obs_module_text("Widget.Text.Font"));
	obs_property_set_long_description(font, obs_module_text("Widget.Text.Font.Description"));

	obs_properties_add_color_alpha(group, key_for(prefix, "color").c_str(), obs_module_text("Bar.Color"));
	obs_properties_add_bool(group, key_for(prefix, "accent").c_str(), obs_module_text("Bar.FollowAccent"));
	obs_properties_add_bool(group, key_for(prefix, "gradient").c_str(), obs_module_text("Widget.Text.Gradient"));
	obs_properties_add_color_alpha(group, key_for(prefix, "color2").c_str(), obs_module_text("Bar.Color2"));

	obs_property_t *angle = obs_properties_add_float_slider(group, key_for(prefix, "gradient_angle").c_str(),
								obs_module_text("Bar.GradientAngle"), 0.0, 360.0, 1.0);
	obs_property_float_set_suffix(angle, "°");

	obs_properties_add_float_slider(group, key_for(prefix, "outline").c_str(),
					obs_module_text("Widget.Text.Outline"), 0.0, 32.0, 0.5);
	obs_properties_add_color_alpha(group, key_for(prefix, "outline_color").c_str(),
				       obs_module_text("Widget.Text.OutlineColor"));

	obs_properties_add_bool(group, key_for(prefix, "shadow").c_str(), obs_module_text("Widget.Shadow"));
	obs_properties_add_float(group, key_for(prefix, "shadow_x").c_str(), obs_module_text("Widget.Shadow.X"), -256.0,
				 256.0, 1.0);
	obs_properties_add_float(group, key_for(prefix, "shadow_y").c_str(), obs_module_text("Widget.Shadow.Y"), -256.0,
				 256.0, 1.0);
	obs_properties_add_float_slider(group, key_for(prefix, "shadow_blur").c_str(),
					obs_module_text("Widget.Shadow.Blur"), 0.0, 64.0, 1.0);
	obs_properties_add_color_alpha(group, key_for(prefix, "shadow_color").c_str(),
				       obs_module_text("Widget.Shadow.Color"));

	obs_property_t *spacing = obs_properties_add_float(group, key_for(prefix, "letter_spacing").c_str(),
							   obs_module_text("Widget.Text.LetterSpacing"), -20.0, 40.0,
							   0.5);
	obs_property_float_set_suffix(spacing, " px");

	obs_property_t *line_height = obs_properties_add_float_slider(group, key_for(prefix, "line_height").c_str(),
								      obs_module_text("Widget.Text.LineHeight"), 50.0,
								      300.0, 5.0);
	obs_property_float_set_suffix(line_height, "%");

	obs_property_t *align = obs_properties_add_list(group, key_for(prefix, "align").c_str(),
							obs_module_text("Widget.Text.Align"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(align, obs_module_text("Widget.Text.Align.Left"), (int64_t)TextAlign::Left);
	obs_property_list_add_int(align, obs_module_text("Widget.Text.Align.Center"), (int64_t)TextAlign::Center);
	obs_property_list_add_int(align, obs_module_text("Widget.Text.Align.Right"), (int64_t)TextAlign::Right);

	obs_property_t *valign = obs_properties_add_list(group, key_for(prefix, "valign").c_str(),
							 obs_module_text("Widget.Text.VAlign"), OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(valign, obs_module_text("Widget.Text.VAlign.Top"), (int64_t)TextVAlign::Top);
	obs_property_list_add_int(valign, obs_module_text("Widget.Text.VAlign.Middle"), (int64_t)TextVAlign::Middle);
	obs_property_list_add_int(valign, obs_module_text("Widget.Text.VAlign.Bottom"), (int64_t)TextVAlign::Bottom);

	obs_property_t *transform = obs_properties_add_list(group, key_for(prefix, "transform").c_str(),
							    obs_module_text("Widget.Text.Transform"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(transform, obs_module_text("Widget.Text.Transform.None"),
				  (int64_t)TextTransform::None);
	obs_property_list_add_int(transform, obs_module_text("Widget.Text.Transform.Upper"),
				  (int64_t)TextTransform::Upper);
	obs_property_list_add_int(transform, obs_module_text("Widget.Text.Transform.Lower"),
				  (int64_t)TextTransform::Lower);
	obs_property_list_add_int(transform, obs_module_text("Widget.Text.Transform.Title"),
				  (int64_t)TextTransform::Title);

	obs_property_t *rotation = obs_properties_add_float_slider(
		group, key_for(prefix, "rotation").c_str(), obs_module_text("Widget.Rotation"), -180.0, 180.0, 1.0);
	obs_property_float_set_suffix(rotation, "°");

	obs_property_t *overflow = obs_properties_add_list(group, key_for(prefix, "overflow").c_str(),
							   obs_module_text("Widget.Text.Overflow"), OBS_COMBO_TYPE_LIST,
							   OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(overflow, obs_module_text("Widget.Text.Overflow.None"), (int64_t)TextOverflow::None);
	obs_property_list_add_int(overflow, obs_module_text("Widget.Text.Overflow.Marquee"),
				  (int64_t)TextOverflow::Marquee);
	obs_property_list_add_int(overflow, obs_module_text("Widget.Text.Overflow.Shrink"),
				  (int64_t)TextOverflow::Shrink);
	obs_property_list_add_int(overflow, obs_module_text("Widget.Text.Overflow.Ellipsis"),
				  (int64_t)TextOverflow::Ellipsis);
	obs_property_list_add_int(overflow, obs_module_text("Widget.Text.Overflow.Wrap"), (int64_t)TextOverflow::Wrap);
	obs_property_set_modified_callback(overflow, overflow_modified);

	obs_property_t *marquee_style = obs_properties_add_list(group, key_for(prefix, "marquee_style").c_str(),
								obs_module_text("Widget.Text.MarqueeStyle"),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(marquee_style, obs_module_text("Widget.Text.MarqueeStyle.Loop"),
				  (int64_t)MarqueeStyle::Loop);
	obs_property_list_add_int(marquee_style, obs_module_text("Widget.Text.MarqueeStyle.Bounce"),
				  (int64_t)MarqueeStyle::Bounce);

	obs_property_t *speed = obs_properties_add_float_slider(group, key_for(prefix, "marquee_speed").c_str(),
								obs_module_text("Widget.Text.MarqueeSpeed"), 5.0, 400.0,
								5.0);
	obs_property_float_set_suffix(speed, " px/s");

	obs_property_t *gap = obs_properties_add_float_slider(group, key_for(prefix, "marquee_gap").c_str(),
							      obs_module_text("Widget.Text.MarqueeGap"), 0.0, 512.0,
							      4.0);
	obs_property_float_set_suffix(gap, " px");

	obs_property_t *pause = obs_properties_add_float_slider(group, key_for(prefix, "marquee_pause").c_str(),
								obs_module_text("Widget.Text.MarqueePause"), 0.0, 10.0,
								0.1);
	obs_property_float_set_suffix(pause, " s");

	obs_property_t *ellipsis = obs_properties_add_list(group, key_for(prefix, "ellipsis_side").c_str(),
							   obs_module_text("Widget.Text.EllipsisSide"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(ellipsis, obs_module_text("Widget.Text.EllipsisSide.Right"),
				  (int64_t)EllipsisSide::Right);
	obs_property_list_add_int(ellipsis, obs_module_text("Widget.Text.EllipsisSide.Left"),
				  (int64_t)EllipsisSide::Left);
	obs_property_list_add_int(ellipsis, obs_module_text("Widget.Text.EllipsisSide.Middle"),
				  (int64_t)EllipsisSide::Middle);

	obs_properties_add_int(group, key_for(prefix, "max_lines").c_str(), obs_module_text("Widget.Text.MaxLines"), 1,
			       32, 1);

	obs_property_t *shrink = obs_properties_add_float_slider(group, key_for(prefix, "shrink_min").c_str(),
								 obs_module_text("Widget.Text.ShrinkMin"), 10.0, 100.0,
								 1.0);
	obs_property_float_set_suffix(shrink, "%");

	const std::string label = std::string(obs_module_text("Widget.Text.Slot")) + ' ' + std::to_string(slot + 1);

	obs_properties_add_group(props, key_for(prefix, "enable").c_str(), label.c_str(), OBS_GROUP_CHECKABLE, group);
}

obs_properties_t *widget_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	MusicLink::add_property(props);

	obs_properties_add_int(props, "width", obs_module_text("Widget.CanvasWidth"), 16, 8192, 1);
	obs_properties_add_int(props, "height", obs_module_text("Widget.CanvasHeight"), 16, 8192, 1);

	obs_property_t *preset = obs_properties_add_list(props, "preset", obs_module_text("Widget.Preset"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(preset, obs_module_text("Widget.Preset.ArtLeft"), (int64_t)LayoutPreset::ArtLeft);
	obs_property_list_add_int(preset, obs_module_text("Widget.Preset.ArtRight"), (int64_t)LayoutPreset::ArtRight);
	obs_property_list_add_int(preset, obs_module_text("Widget.Preset.VerticalCard"),
				  (int64_t)LayoutPreset::VerticalCard);
	obs_property_list_add_int(preset, obs_module_text("Widget.Preset.WideBanner"),
				  (int64_t)LayoutPreset::WideBanner);
	obs_property_list_add_int(preset, obs_module_text("Widget.Preset.CompactBar"),
				  (int64_t)LayoutPreset::CompactBar);
	obs_property_list_add_int(preset, obs_module_text("Widget.Preset.TextOnly"), (int64_t)LayoutPreset::TextOnly);
	obs_property_set_long_description(preset, obs_module_text("Widget.Preset.Description"));

	obs_properties_add_button2(props, "apply_preset", obs_module_text("Widget.Preset.Apply"), preset_clicked, data);

	obs_property_t *time_format = obs_properties_add_list(props, "time_format", obs_module_text("Info.TimeFormat"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.Auto"), (int64_t)TimeFormat::Auto);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.MSS"), (int64_t)TimeFormat::MSS);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.MMSS"), (int64_t)TimeFormat::MMSS);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.HMMSS"), (int64_t)TimeFormat::HMMSS);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.HHMMSS"), (int64_t)TimeFormat::HHMMSS);

	obs_property_t *transition = obs_properties_add_list(props, "transition", obs_module_text("Widget.Transition"),
							     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(transition, obs_module_text("Widget.Transition.None"),
				  (int64_t)TrackTransition::None);
	obs_property_list_add_int(transition, obs_module_text("Widget.Transition.CrossFade"),
				  (int64_t)TrackTransition::CrossFade);
	obs_property_list_add_int(transition, obs_module_text("Widget.Transition.Slide"),
				  (int64_t)TrackTransition::Slide);
	obs_property_list_add_int(transition, obs_module_text("Widget.Transition.FadeThroughBlank"),
				  (int64_t)TrackTransition::FadeThroughBlank);

	obs_property_t *transition_ms = obs_properties_add_int_slider(
		props, "transition_ms", obs_module_text("Widget.TransitionDuration"), 0, 3000, 10);
	obs_property_int_set_suffix(transition_ms, " ms");

	obs_property_t *idle = obs_properties_add_list(props, "idle_mode", obs_module_text("Widget.Idle"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(idle, obs_module_text("Widget.Idle.Hide"), (int64_t)WidgetIdle::Hide);
	obs_property_list_add_int(idle, obs_module_text("Widget.Idle.Placeholder"), (int64_t)WidgetIdle::Placeholder);
	obs_property_list_add_int(idle, obs_module_text("Widget.Idle.Freeze"), (int64_t)WidgetIdle::Freeze);
	obs_property_set_long_description(idle, obs_module_text("Widget.Idle.Description"));

	obs_property_t *idle_trigger = obs_properties_add_list(props, "idle_trigger",
							       obs_module_text("Common.IdleTrigger"),
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(idle_trigger, obs_module_text("Common.IdleTrigger.Stopped"),
				  (int64_t)WidgetIdleTrigger::Stopped);
	obs_property_list_add_int(idle_trigger, obs_module_text("Common.IdleTrigger.StoppedOrPaused"),
				  (int64_t)WidgetIdleTrigger::StoppedOrPaused);
	obs_property_set_long_description(idle_trigger, obs_module_text("Common.IdleTrigger.Description"));

	add_background_properties(props);
	add_art_properties(props);

	for (int slot = 0; slot < TEXT_SLOTS; slot++)
		add_text_properties(props, slot);

	obs_properties_t *bar_group = obs_properties_create();
	add_geometry_properties(bar_group, "bar_", false);
	BarRenderer::add_properties(bar_group, "bar_", false, true);
	obs_properties_add_group(props, "bar_enable", obs_module_text("Widget.Bar"), OBS_GROUP_CHECKABLE, bar_group);

	return props;
}

struct obs_source_info music_widget_info = {};

} // namespace

void register_music_widget_source()
{
	music_widget_info.id = MUSIC_WIDGET_SOURCE_ID;
	music_widget_info.type = OBS_SOURCE_TYPE_INPUT;
	music_widget_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
	music_widget_info.icon_type = OBS_ICON_TYPE_MEDIA;
	music_widget_info.get_name = widget_get_name;
	music_widget_info.create = widget_create;
	music_widget_info.destroy = widget_destroy;
	music_widget_info.update = widget_update;
	music_widget_info.get_defaults = widget_defaults;
	music_widget_info.get_properties = widget_properties;
	music_widget_info.get_width = widget_get_width;
	music_widget_info.get_height = widget_get_height;
	music_widget_info.video_tick = widget_tick;
	music_widget_info.video_render = widget_render;
	music_widget_info.enum_active_sources = widget_enum_active_sources;

	obs_register_source(&music_widget_info);
}

} // namespace vr
