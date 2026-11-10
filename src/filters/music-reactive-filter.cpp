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

#include "filters/music-reactive-filter.hpp"
#include "audio/beat-detector.hpp"
#include "music/music-api.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/matrix4.h>
#include <media-io/audio-io.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace vr {

namespace {

constexpr const char *FILTER_ID = "void_radio_music_reactive";
constexpr float STALE_SECONDS = 0.15f;
constexpr size_t MAX_PENDING = 48000; /* a second, should ticks stall */
constexpr float PI_F = 3.14159265358979323846f;

/* Stored in settings; the numbers must not change. */
enum class Drive {
	Beat = 0,
	Bass = 1,
	Level = 2,
	Treble = 3,
};

struct ReactiveFilter {
	obs_source_t *self = nullptr;
	gs_effect_t *effect = nullptr;
	MusicLink link;

	/* Audio from the music source, mixed to mono on OBS's audio thread and
	 * taken by the tick. */
	std::mutex mutex;
	std::vector<float> pending;
	uint64_t frames = 0;
	obs_weak_source_t *attached = nullptr;

	/* Tick only. */
	BeatDetector detector{48000};
	uint32_t detector_rate = 48000;
	std::vector<float> work;
	uint64_t last_frames = 0;
	float stale = 0.0f;

	/* The level modes stretch between where the music has recently sat
	 * low and high, so a loud master still moves. */
	float low = 0.0f;
	float high = 1.0f;

	float drive = 0.0f;
	float time = 0.0f;

	/* Settings. */
	Drive source = Drive::Beat;
	float sensitivity = 1.0f;
	float response = 0.2f; /* seconds to settle back */

	bool scale_on = true;
	float scale_amount = 0.08f;
	bool scale_inside = true;

	bool bright_on = false;
	float bright_amount = 0.3f;

	bool tint_on = false;
	uint32_t tint_color = 0xFFFFFFFF;
	float tint_amount = 0.5f;

	bool fade_on = false;
	float fade_min = 0.3f;

	bool shake_on = false;
	float shake_px = 6.0f;
};

void capture(void *param, obs_source_t *, const struct audio_data *audio, bool)
{
	ReactiveFilter *filter = static_cast<ReactiveFilter *>(param);
	if (!audio || !audio->frames)
		return;

	size_t channels = 0;
	for (size_t plane = 0; plane < MAX_AV_PLANES; plane++)
		if (audio->data[plane])
			channels = plane + 1;
	if (!channels)
		return;

	std::lock_guard<std::mutex> lock(filter->mutex);
	for (uint32_t i = 0; i < audio->frames; i++) {
		float sum = 0.0f;
		for (size_t ch = 0; ch < channels; ch++) {
			const float *data = reinterpret_cast<const float *>(audio->data[ch]);
			if (data)
				sum += data[i];
		}
		filter->pending.push_back(sum / (float)channels);
	}

	if (filter->pending.size() > MAX_PENDING)
		filter->pending.erase(filter->pending.begin(),
				      filter->pending.begin() + (ptrdiff_t)(filter->pending.size() - MAX_PENDING));
	filter->frames += audio->frames;
}

/* Follows the linked music source, hooking the tap off whatever it was on. */
void attach(ReactiveFilter *filter)
{
	obs_source_t *target = filter->link.get();
	obs_source_t *current = filter->attached ? obs_weak_source_get_source(filter->attached) : nullptr;

	if (target == current && (target || !filter->attached)) {
		obs_source_release(target);
		obs_source_release(current);
		return;
	}

	if (current)
		obs_source_remove_audio_capture_callback(current, capture, filter);
	obs_weak_source_release(filter->attached);
	filter->attached = nullptr;

	if (target) {
		{
			std::lock_guard<std::mutex> lock(filter->mutex);
			filter->pending.clear();
		}
		obs_source_add_audio_capture_callback(target, capture, filter);
		filter->attached = obs_source_get_weak_source(target);
	}

	obs_source_release(target);
	obs_source_release(current);
}

void detach(ReactiveFilter *filter)
{
	obs_source_t *current = filter->attached ? obs_weak_source_get_source(filter->attached) : nullptr;
	if (current)
		obs_source_remove_audio_capture_callback(current, capture, filter);
	obs_source_release(current);
	obs_weak_source_release(filter->attached);
	filter->attached = nullptr;
}

/* ------------------------------------------------------------------------- */
/* OBS callbacks                                                              */
/* ------------------------------------------------------------------------- */

const char *filter_name(void *)
{
	return obs_module_text("Reactive");
}

void filter_update(void *data, obs_data_t *settings)
{
	ReactiveFilter *filter = static_cast<ReactiveFilter *>(data);
	filter->link.update(settings);

	filter->source = (Drive)obs_data_get_int(settings, "react_to");
	filter->sensitivity = (float)obs_data_get_double(settings, "sensitivity") / 100.0f;
	filter->response = (float)obs_data_get_int(settings, "response_ms") / 1000.0f;

	filter->scale_on = obs_data_get_bool(settings, "scale_enable");
	filter->scale_amount = (float)obs_data_get_double(settings, "scale_amount") / 100.0f;
	filter->scale_inside = obs_data_get_bool(settings, "scale_inside");

	filter->bright_on = obs_data_get_bool(settings, "bright_enable");
	filter->bright_amount = (float)obs_data_get_double(settings, "bright_amount") / 100.0f;

	filter->tint_on = obs_data_get_bool(settings, "tint_enable");
	filter->tint_color = (uint32_t)obs_data_get_int(settings, "tint_color");
	filter->tint_amount = (float)obs_data_get_double(settings, "tint_amount") / 100.0f;

	filter->fade_on = obs_data_get_bool(settings, "fade_enable");
	filter->fade_min = (float)obs_data_get_double(settings, "fade_min") / 100.0f;

	filter->shake_on = obs_data_get_bool(settings, "shake_enable");
	filter->shake_px = (float)obs_data_get_double(settings, "shake_px");

	filter->detector.set_sensitivity(filter->sensitivity);
	filter->detector.set_pulse_decay(std::max(0.05f, filter->response));
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	ReactiveFilter *filter = new ReactiveFilter();
	filter->self = source;

	if (audio_t *audio = obs_get_audio()) {
		filter->detector_rate = audio_output_get_sample_rate(audio);
		filter->detector = BeatDetector(filter->detector_rate);
	}

	char *path = obs_module_file("effects/music-reactive.effect");
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, nullptr);
	obs_leave_graphics();
	bfree(path);

	if (!filter->effect)
		obs_log(LOG_ERROR, "failed to load effects/music-reactive.effect");

	filter_update(filter, settings);
	return filter;
}

void filter_destroy(void *data)
{
	ReactiveFilter *filter = static_cast<ReactiveFilter *>(data);

	detach(filter);
	filter->link.release();

	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();

	delete filter;
}

void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "react_to", (int64_t)Drive::Beat);
	obs_data_set_default_double(settings, "sensitivity", 100.0);
	obs_data_set_default_int(settings, "response_ms", 250);

	obs_data_set_default_bool(settings, "scale_enable", true);
	obs_data_set_default_double(settings, "scale_amount", 8.0);
	obs_data_set_default_bool(settings, "scale_inside", true);

	obs_data_set_default_bool(settings, "bright_enable", false);
	obs_data_set_default_double(settings, "bright_amount", 30.0);

	obs_data_set_default_bool(settings, "tint_enable", false);
	obs_data_set_default_int(settings, "tint_color", 0xFFFF6C7C);
	obs_data_set_default_double(settings, "tint_amount", 50.0);

	obs_data_set_default_bool(settings, "fade_enable", false);
	obs_data_set_default_double(settings, "fade_min", 30.0);

	obs_data_set_default_bool(settings, "shake_enable", false);
	obs_data_set_default_double(settings, "shake_px", 6.0);
}

obs_properties_t *filter_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	MusicLink::add_property(props);

	obs_property_t *drive = obs_properties_add_list(props, "react_to", obs_module_text("Reactive.ReactTo"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(drive, obs_module_text("Reactive.ReactTo.Beat"), (int64_t)Drive::Beat);
	obs_property_list_add_int(drive, obs_module_text("Reactive.ReactTo.Bass"), (int64_t)Drive::Bass);
	obs_property_list_add_int(drive, obs_module_text("Reactive.ReactTo.Level"), (int64_t)Drive::Level);
	obs_property_list_add_int(drive, obs_module_text("Reactive.ReactTo.Treble"), (int64_t)Drive::Treble);
	obs_property_set_long_description(drive, obs_module_text("Reactive.ReactTo.Description"));

	obs_property_t *sensitivity = obs_properties_add_float_slider(
		props, "sensitivity", obs_module_text("Widget.Viz.Sensitivity"), 25.0, 400.0, 5.0);
	obs_property_float_set_suffix(sensitivity, "%");

	obs_property_t *response =
		obs_properties_add_int_slider(props, "response_ms", obs_module_text("Reactive.Response"), 50, 2000, 10);
	obs_property_int_set_suffix(response, " ms");
	obs_property_set_long_description(response, obs_module_text("Reactive.Response.Description"));

	obs_properties_t *scale = obs_properties_create();
	obs_property_t *amount = obs_properties_add_float_slider(scale, "scale_amount",
								 obs_module_text("Reactive.Amount"), 1.0, 50.0, 0.5);
	obs_property_float_set_suffix(amount, "%");
	obs_property_t *inside =
		obs_properties_add_bool(scale, "scale_inside", obs_module_text("Reactive.Scale.Inside"));
	obs_property_set_long_description(inside, obs_module_text("Reactive.Scale.Inside.Description"));
	obs_properties_add_group(props, "scale_enable", obs_module_text("Reactive.Scale"), OBS_GROUP_CHECKABLE, scale);

	obs_properties_t *bright = obs_properties_create();
	obs_property_t *bright_amount = obs_properties_add_float_slider(
		bright, "bright_amount", obs_module_text("Reactive.Amount"), 1.0, 100.0, 1.0);
	obs_property_float_set_suffix(bright_amount, "%");
	obs_properties_add_group(props, "bright_enable", obs_module_text("Reactive.Bright"), OBS_GROUP_CHECKABLE,
				 bright);

	obs_properties_t *tint = obs_properties_create();
	obs_properties_add_color(tint, "tint_color", obs_module_text("Bar.Color"));
	obs_property_t *tint_amount = obs_properties_add_float_slider(
		tint, "tint_amount", obs_module_text("Reactive.Amount"), 1.0, 100.0, 1.0);
	obs_property_float_set_suffix(tint_amount, "%");
	obs_properties_add_group(props, "tint_enable", obs_module_text("Reactive.Tint"), OBS_GROUP_CHECKABLE, tint);

	obs_properties_t *fade = obs_properties_create();
	obs_property_t *fade_min =
		obs_properties_add_float_slider(fade, "fade_min", obs_module_text("Reactive.Fade.Min"), 0.0, 95.0, 1.0);
	obs_property_float_set_suffix(fade_min, "%");
	obs_properties_add_group(props, "fade_enable", obs_module_text("Reactive.Fade"), OBS_GROUP_CHECKABLE, fade);

	obs_properties_t *shake = obs_properties_create();
	obs_property_t *shake_px =
		obs_properties_add_float_slider(shake, "shake_px", obs_module_text("Reactive.Amount"), 1.0, 64.0, 1.0);
	obs_property_float_set_suffix(shake_px, " px");
	obs_properties_add_group(props, "shake_enable", obs_module_text("Reactive.Shake"), OBS_GROUP_CHECKABLE, shake);

	return props;
}

void filter_tick(void *data, float seconds)
{
	ReactiveFilter *filter = static_cast<ReactiveFilter *>(data);

	filter->link.tick(seconds);
	attach(filter);
	filter->time += seconds;

	uint64_t frames = 0;
	{
		std::lock_guard<std::mutex> lock(filter->mutex);
		filter->work.swap(filter->pending);
		filter->pending.clear();
		frames = filter->frames;
	}

	if (!filter->work.empty())
		filter->detector.process(filter->work.data(), filter->work.size());
	filter->work.clear();

	/* Paused music stops arriving rather than going quiet. */
	if (frames != filter->last_frames) {
		filter->last_frames = frames;
		filter->stale = 0.0f;
	} else {
		filter->stale += seconds;
		if (filter->stale > STALE_SECONDS)
			filter->detector.idle(seconds);
	}

	float target = 0.0f;

	if (filter->source == Drive::Beat) {
		target = filter->detector.pulse();
	} else {
		const float value = filter->source == Drive::Bass     ? filter->detector.bass()
				    : filter->source == Drive::Treble ? filter->detector.treble()
								      : filter->detector.level();

		/* Stretch between the recent low and high, which move slowly, so
		 * the effect swings with the music rather than sitting near full
		 * on a loud track. */
		const float drift = 1.0f - std::exp(-seconds / 2.0f);
		filter->high = value > filter->high ? value : filter->high + (value - filter->high) * drift;
		filter->low = value < filter->low ? value : filter->low + (value - filter->low) * drift;

		const float span = std::max(0.05f, filter->high - filter->low);
		target = std::clamp((value - filter->low) / span * filter->sensitivity, 0.0f, 1.0f);
	}

	/* Up at once, down over the response time. */
	if (target >= filter->drive)
		filter->drive = target;
	else
		filter->drive = target + (filter->drive - target) *
						 std::exp(-seconds / std::max(0.01f, filter->response * 0.5f));
}

void filter_render(void *data, gs_effect_t *)
{
	ReactiveFilter *filter = static_cast<ReactiveFilter *>(data);

	obs_source_t *target = obs_filter_get_target(filter->self);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;

	const bool any = filter->scale_on || filter->bright_on || filter->tint_on || filter->fade_on ||
			 filter->shake_on;
	if (!filter->effect || !width || !height || !any) {
		obs_source_skip_video_filter(filter->self);
		return;
	}

	const float d = filter->drive;

	const enum gs_color_space preferred[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
	const enum gs_color_space space = obs_source_get_color_space(target, 3, preferred);
	const enum gs_color_format format = gs_get_format_from_space(space);

	if (!obs_source_process_filter_begin_with_color_space(filter->self, format, space, OBS_ALLOW_DIRECT_RENDERING))
		return;

	const float brightness = filter->bright_on ? filter->bright_amount * d : 0.0f;
	const float opacity = filter->fade_on ? filter->fade_min + (1.0f - filter->fade_min) * d : 1.0f;

	struct vec4 tint;
	vec4_from_rgba(&tint, filter->tint_color);
	tint.w = filter->tint_on ? filter->tint_amount * d : 0.0f;

	gs_effect_set_float(gs_effect_get_param_by_name(filter->effect, "brightness"), brightness);
	gs_effect_set_vec4(gs_effect_get_param_by_name(filter->effect, "tint"), &tint);
	gs_effect_set_float(gs_effect_get_param_by_name(filter->effect, "opacity"), opacity);

	/* The size pulse grows from the middle. Kept inside its box, it rests a
	 * little smaller and grows to full size, so nothing is ever cut off. */
	float scale = 1.0f;
	if (filter->scale_on)
		scale = filter->scale_inside ? 1.0f - filter->scale_amount * (1.0f - d)
					     : 1.0f + filter->scale_amount * d;

	float dx = 0.0f;
	float dy = 0.0f;
	if (filter->shake_on) {
		/* Two unrelated wobbles, so it shakes rather than circles. */
		const float t = filter->time;
		dx = filter->shake_px * d * std::sin(t * 2.0f * PI_F * 13.0f) * std::cos(t * 2.0f * PI_F * 3.1f);
		dy = filter->shake_px * d * std::sin(t * 2.0f * PI_F * 11.0f + 1.3f) * std::cos(t * 2.0f * PI_F * 2.3f);
	}

	gs_matrix_push();
	gs_matrix_translate3f((float)width * 0.5f + dx, (float)height * 0.5f + dy, 0.0f);
	gs_matrix_scale3f(scale, scale, 1.0f);
	gs_matrix_translate3f(-(float)width * 0.5f, -(float)height * 0.5f, 0.0f);

	obs_source_process_filter_end(filter->self, filter->effect, width, height);

	gs_matrix_pop();
}

struct obs_source_info reactive_filter_info = {};

} // namespace

void register_music_reactive_filter()
{
	reactive_filter_info.id = FILTER_ID;
	reactive_filter_info.type = OBS_SOURCE_TYPE_FILTER;
	reactive_filter_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	reactive_filter_info.get_name = filter_name;
	reactive_filter_info.create = filter_create;
	reactive_filter_info.destroy = filter_destroy;
	reactive_filter_info.update = filter_update;
	reactive_filter_info.get_defaults = filter_defaults;
	reactive_filter_info.get_properties = filter_properties;
	reactive_filter_info.video_tick = filter_tick;
	reactive_filter_info.video_render = filter_render;

	obs_register_source(&reactive_filter_info);
}

} // namespace vr
