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
#include "sources/music-widget/widget-design.hpp"
#include "sources/music-widget/widget.hpp"

#include <plugin-support.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vr {

namespace widget {

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

static const char *widget_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MusicWidget");
}

static uint32_t widget_get_width(void *data)
{
	return static_cast<MusicWidget *>(data)->width;
}

static uint32_t widget_get_height(void *data)
{
	return static_cast<MusicWidget *>(data)->height;
}

static void *widget_create(obs_data_t *settings, obs_source_t *source)
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

	migrate_settings(settings);
	widget_update(widget, settings);
	register_motion_hotkey(widget);

	return widget;
}

static void widget_destroy(void *data)
{
	MusicWidget *widget = static_cast<MusicWidget *>(data);

	unregister_motion_hotkey(widget);
	detach_audio(widget);
	obs_weak_source_release(widget->tap.attached);
	widget->tap.attached = nullptr;

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

	if (widget->glow_texture)
		gs_texture_destroy(widget->glow_texture);

	if (widget->previous)
		gs_texture_destroy(widget->previous);

	if (widget->background.image_valid)
		vr_image_file_free(&widget->background.image);

	destroy_extras(widget);

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
static bool resolve_idle(const MusicWidget *widget)
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
static const Snapshot *effective_snapshot(const MusicWidget *widget)
{
	if (!widget->idle)
		return widget->have_snapshot ? &widget->snapshot : nullptr;

	if (widget->idle_mode == WidgetIdle::Freeze && widget->have_frozen)
		return &widget->frozen;

	return widget->have_snapshot ? &widget->snapshot : nullptr;
}

static std::string expand_for(const MusicWidget *widget, const TextElement &element)
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

static void refresh_text(MusicWidget *widget, TextElement &element)
{
	const std::string next = expand_for(widget, element);

	TextStyle style = element.style;
	style.color = resolve_color(widget, style.color, element.follow_accent, element.tone);
	style.shadow_color = resolve_color(widget, style.shadow_color, element.shadow_accent);

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

static void collect_art_result(MusicWidget *widget)
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

	if (result.have_glow) {
		widget->pending_glow = std::move(result.glow);
		widget->upload_glow = true;
	}
}

static void request_art(MusicWidget *widget)
{
	const Snapshot *snapshot = effective_snapshot(widget);

	/* Showing the placeholder means letting go of the last track's cover,
	 * so the loader is pointed at nothing and falls through to it. */
	const bool use_placeholder_only = widget->idle && widget->idle_mode == WidgetIdle::Placeholder;

	const std::string path = (snapshot && !use_placeholder_only) ? snapshot->path : std::string();
	const int sample = widget->sample_cover;

	const bool want_blur = widget->background.enabled && widget->background.type == BackgroundType::BlurredArt;

	/* The glow is blurred small and scaled up, which is both cheaper and
	 * softer; its size only steps in sixty-fours so resizing the box does
	 * not ask for a new one on every pixel. */
	uint32_t glow_size = 0;
	if (widget->glow.geometry.enabled) {
		const float side = std::max(widget->glow.geometry.width, widget->glow.geometry.height);
		glow_size = (uint32_t)std::clamp((int)std::ceil(side / 64.0f) * 64 / 2, 32, 256);
	}

	if (!widget->art_settings_dirty && path == widget->requested_path &&
	    widget->art.placeholder == widget->requested_placeholder && sample == widget->requested_sample &&
	    glow_size == widget->requested_glow)
		return;

	widget->requested_glow = glow_size;
	widget->requested_sample = sample;
	widget->requested_path = path;
	widget->requested_placeholder = widget->art.placeholder;
	widget->art_settings_dirty = false;

	ArtLoader::Request request;
	request.path = path;
	request.placeholder = widget->art.placeholder;
	request.sample = sample;
	request.glow_size = glow_size;
	request.want_blur = want_blur;
	request.blur_width = widget->width;
	request.blur_height = widget->height;
	request.blur_strength = widget->background.blur / 100.0f;
	request.blur_darken = widget->background.darken / 100.0f;
	request.blur_desaturate = widget->background.desaturate / 100.0f;

	widget->loader.request(request);
}

/* The editor's made-up track: two of them, so a track change can be shown,
 * with a long-titled version of each for trying out the long text options. */
static void fill_sample_snapshot(const PreviewTrack &preview, Snapshot &out)
{
	struct Sample {
		const char *title;
		const char *long_title;
		const char *artist;
		const char *album;
		const char *year;
		const char *genre;
		const char *bpm;
		int64_t duration_ms;
	};

	static const Sample SAMPLES[2] = {
		{"Midnight Signal", "Midnight Signal (Extended Night Drive Mix) [Remastered Anniversary Edition]",
		 "The Void Collective", "Static Bloom", "2026", "Synthwave", "118", 214000},
		{"Low Orbit", "Low Orbit (Live from the Northern Frequencies Tour, Second Night, Encore)",
		 "Aurora Lane", "Northern Frequencies", "2025", "Dream Pop", "96", 187000},
	};

	const Sample &now = SAMPLES[preview.serial % 2];
	const Sample &next = SAMPLES[(preview.serial + 1) % 2];

	out = Snapshot();
	out.valid = true;
	out.state = PlayState::Playing;
	out.title = preview.long_text ? now.long_title : now.title;
	out.artist = now.artist;
	out.album = now.album;
	out.album_artist = now.artist;
	out.year = now.year;
	out.genre = now.genre;
	out.track = "3";
	out.disc = "1";
	out.bpm = now.bpm;
	out.composer = now.artist;
	out.codec = "FLAC";
	out.bitrate = "1024 kbps";
	out.samplerate = "44.1 kHz";
	out.next_title = preview.long_text ? next.long_title : next.title;
	out.next_artist = next.artist;
	out.next_album = next.album;
	out.index = 2;
	out.count = 12;
	out.duration_ms = now.duration_ms;
	out.elapsed_ms = (int64_t)(std::fmod(preview.time, (float)now.duration_ms / 1000.0f) * 1000.0f);
	out.progress = (float)out.elapsed_ms / (float)out.duration_ms;
	out.seekable = true;
	out.track_serial = preview.serial;
}

/* A track has started: the transition between the old card and the new, the
 * pop-in and the entrances. */
static void on_track_start(MusicWidget *widget)
{
	if (widget->transition != TrackTransition::None && widget->transition_seconds > 0.0f &&
	    widget->previous_valid) {
		widget->transitioning = true;
		widget->transition_time = 0.0f;
	}

	trigger_popup(widget);
	trigger_entrance(widget);
}

static void widget_tick(void *data, float seconds)
{
	MusicWidget *widget = static_cast<MusicWidget *>(data);

	/* Playback state. The link keeps looking for its music source, so a
	 * widget loaded before the source it points at still finds it. */
	widget->link.tick(seconds);

	bool preview_active = false;
	bool replay = false;
	{
		std::lock_guard<std::mutex> lock(widget->preview_mutex);
		PreviewTrack &preview = widget->preview;
		preview_active = preview.active;
		replay = widget->replay_request;
		widget->replay_request = false;

		if (preview_active) {
			if (replay)
				preview.serial++;
			preview.time += seconds;
			fill_sample_snapshot(preview, widget->preview_snapshot);
		}

		widget->sample_cover = preview_active ? (int)(preview.serial % 2) + 1 : 0;
	}

	widget->have_snapshot = false;

	if (preview_active) {
		widget->snapshot = widget->preview_snapshot;
		widget->have_snapshot = true;
	} else if (obs_source_t *source = widget->link.get()) {
		widget->have_snapshot = get_snapshot(source, widget->snapshot);
		obs_source_release(source);
	}

	if (!preview_active && widget->have_snapshot && widget->snapshot.state != PlayState::Stopped) {
		widget->frozen = widget->snapshot;
		widget->have_frozen = true;
	}

	/* Decided before anything reads it: what the widget shows while idle is
	 * chosen from the live state, not from the snapshot it settles on. */
	const bool was_idle = widget->idle;
	widget->idle = resolve_idle(widget);

	const Snapshot *snapshot = effective_snapshot(widget);
	const bool playing = !widget->idle;

	/* Track changes drive both the transition and a fresh art request. */
	if (snapshot) {
		if (!widget->have_serial) {
			widget->have_serial = true;
			widget->last_serial = snapshot->track_serial;
			if (playing)
				on_track_start(widget);
		} else if (snapshot->track_serial != widget->last_serial) {
			widget->last_serial = snapshot->track_serial;
			on_track_start(widget);
		} else if (was_idle && playing) {
			/* Starting again after a stop is a track starting too, as
			 * far as showing the card goes. */
			trigger_popup(widget);
			trigger_entrance(widget);
		}
	}

	if (replay && !preview_active)
		on_track_start(widget);

	{
		std::lock_guard<std::mutex> lock(widget->preview_mutex);
		widget->have_shown = snapshot != nullptr;
		if (snapshot)
			widget->shown_snapshot = *snapshot;
	}

	/* With nothing playing, a card that only shows itself on track changes
	 * gets out of the way straight off. */
	if (widget->idle && widget->popup_mode == PopupMode::OnTrackChange &&
	    widget->popup_time < widget->popup_seconds + widget->popup_hold)
		widget->popup_time = std::max(widget->popup_time, widget->popup_seconds + widget->popup_hold);

	tick_motion(widget, seconds);

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
		destroy_texture(widget->glow_texture);
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

	if (widget->upload_glow) {
		destroy_texture(widget->glow_texture);
		widget->glow_texture = create_texture(widget->pending_glow);
		widget->pending_glow.clear();
		widget->upload_glow = false;
	}

	refresh_palette(widget);

	uint32_t accent = 0;
	const bool have_accent = current_accent(widget, accent);
	widget->bar.set_accent(accent, have_accent);
	widget->bar.tick(seconds, snapshot);

	tick_extras(widget);
	tick_visualizer(widget, seconds, playing, preview_active);

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

static MusicWidget *widget_of(obs_source_t *source)
{
	const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
	if (!id || strcmp(id, MUSIC_WIDGET_SOURCE_ID) != 0)
		return nullptr;
	return static_cast<MusicWidget *>(obs_obj_get_data(source));
}

} // namespace widget

namespace {

struct obs_source_info music_widget_info = {};

} // namespace

void set_widget_sample_track(obs_source_t *source, bool active, bool long_text)
{
	widget::MusicWidget *widget = widget::widget_of(source);
	if (!widget)
		return;

	std::lock_guard<std::mutex> lock(widget->preview_mutex);
	if (active && !widget->preview.active)
		widget->preview.time = 20.0f;
	widget->preview.active = active;
	widget->preview.long_text = long_text;
}

bool get_widget_snapshot(obs_source_t *source, Snapshot &out)
{
	widget::MusicWidget *widget = widget::widget_of(source);
	if (!widget)
		return false;

	std::lock_guard<std::mutex> lock(widget->preview_mutex);
	if (!widget->have_shown)
		return false;
	out = widget->shown_snapshot;
	return true;
}

void replay_widget_track_change(obs_source_t *source)
{
	widget::MusicWidget *widget = widget::widget_of(source);
	if (!widget)
		return;

	std::lock_guard<std::mutex> lock(widget->preview_mutex);
	widget->replay_request = true;
}

void register_music_widget_source()
{
	music_widget_info.id = MUSIC_WIDGET_SOURCE_ID;
	music_widget_info.type = OBS_SOURCE_TYPE_INPUT;
	music_widget_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
	music_widget_info.icon_type = OBS_ICON_TYPE_MEDIA;
	music_widget_info.get_name = widget::widget_get_name;
	music_widget_info.create = widget::widget_create;
	music_widget_info.destroy = widget::widget_destroy;
	music_widget_info.update = widget::widget_update;
	music_widget_info.get_defaults = widget::widget_defaults;
	music_widget_info.get_properties = widget::widget_properties;
	music_widget_info.get_width = widget::widget_get_width;
	music_widget_info.get_height = widget::widget_get_height;
	music_widget_info.video_tick = widget::widget_tick;
	music_widget_info.video_render = widget::widget_render;
	music_widget_info.enum_active_sources = widget::widget_enum_active_sources;

	obs_register_source(&music_widget_info);
}

} // namespace vr
