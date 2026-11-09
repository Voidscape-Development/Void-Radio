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

/* The audio visualiser: tapping the music source's output, analysing it, and
 * drawing bars, a mirrored spectrum, a waveform or a ring. */

#include "sources/music-widget/widget.hpp"

#include <media-io/audio-io.h>

#include <algorithm>
#include <cmath>

namespace vr::widget {

namespace {

constexpr float STALE_SECONDS = 0.15f;

/* OBS's audio thread. Mixes down to mono into the ring. */
void capture_audio(void *param, obs_source_t *, const struct audio_data *audio, bool)
{
	MusicWidget *widget = static_cast<MusicWidget *>(param);
	AudioTap &tap = widget->tap;

	if (!audio || !audio->frames)
		return;

	size_t channels = 0;
	for (size_t plane = 0; plane < MAX_AV_PLANES; plane++) {
		if (audio->data[plane])
			channels = plane + 1;
	}
	if (!channels)
		return;

	const float inverse = 1.0f / (float)channels;

	std::lock_guard<std::mutex> lock(tap.mutex);

	for (uint32_t frame = 0; frame < audio->frames; frame++) {
		float sum = 0.0f;
		for (size_t channel = 0; channel < channels; channel++) {
			const float *plane = reinterpret_cast<const float *>(audio->data[channel]);
			if (plane)
				sum += plane[frame];
		}

		tap.ring[tap.write] = sum * inverse;
		tap.write = (tap.write + 1) % tap.ring.size();
	}

	tap.frames += audio->frames;
}

/* Hooks the tap onto the linked music source, and off whatever it was on
 * before, when the link changes or the visualiser is switched on or off. */
void attach_audio(MusicWidget *widget, bool want)
{
	AudioTap &tap = widget->tap;

	obs_source_t *target = want ? widget->link.get() : nullptr;
	obs_source_t *current = tap.attached ? obs_weak_source_get_source(tap.attached) : nullptr;

	if (target == current && (target || !tap.attached)) {
		obs_source_release(target);
		obs_source_release(current);
		return;
	}

	if (current)
		obs_source_remove_audio_capture_callback(current, capture_audio, widget);

	obs_weak_source_release(tap.attached);
	tap.attached = nullptr;

	if (target) {
		{
			std::lock_guard<std::mutex> lock(tap.mutex);
			std::fill(tap.ring.begin(), tap.ring.end(), 0.0f);
			tap.write = 0;
		}

		if (audio_t *audio = obs_get_audio())
			tap.sample_rate = audio_output_get_sample_rate(audio);

		obs_source_add_audio_capture_callback(target, capture_audio, widget);
		tap.attached = obs_source_get_weak_source(target);
	}

	obs_source_release(target);
	obs_source_release(current);
}

/* The ring, oldest sample first. */
uint64_t copy_ring(AudioTap &tap, std::vector<float> &out)
{
	std::lock_guard<std::mutex> lock(tap.mutex);

	const size_t size = tap.ring.size();
	out.resize(size);
	for (size_t i = 0; i < size; i++)
		out[i] = tap.ring[(tap.write + i) % size];

	return tap.frames;
}

/* Something plausible to look at in the editor's sample track: a spectrum
 * that falls off towards the treble and moves like music. */
void synthetic_levels(const VisualizerElement &viz, int bands, std::vector<float> &out)
{
	out.resize((size_t)bands);
	const float beat = 0.55f + 0.45f * std::pow(std::max(0.0f, std::sin(viz.time * 4.2f)), 6.0f);

	for (int i = 0; i < bands; i++) {
		const float f = (float)i / (float)std::max(1, bands - 1);
		const float shape = 0.9f - 0.55f * f;
		const float wobble =
			0.65f + 0.35f * std::sin(viz.time * (2.3f + (float)(i % 7) * 0.6f) + (float)i * 1.7f);
		out[(size_t)i] = std::clamp(shape * wobble * (f < 0.25f ? beat : 0.85f), 0.0f, 1.0f);
	}
}

void synthetic_wave(const VisualizerElement &viz, int points, std::vector<float> &out)
{
	out.resize((size_t)points);
	for (int i = 0; i < points; i++) {
		const float t = (float)i / (float)std::max(1, points - 1);
		out[(size_t)i] = 0.55f * std::sin(t * 18.0f + viz.time * 6.0f) * std::sin(t * 3.14159f) +
				 0.2f * std::sin(t * 61.0f - viz.time * 9.0f);
	}
}

} // namespace

void read_visualizer(MusicWidget *widget, obs_data_t *settings)
{
	VisualizerElement &viz = widget->viz;

	viz.geometry.enabled = obs_data_get_bool(settings, "viz_enable");
	viz.geometry.anchor = (Anchor)obs_data_get_int(settings, "viz_anchor");
	viz.geometry.x = (float)obs_data_get_double(settings, "viz_x");
	viz.geometry.y = (float)obs_data_get_double(settings, "viz_y");
	viz.geometry.width = (float)std::max<int64_t>(1, obs_data_get_int(settings, "viz_width"));
	viz.geometry.height = (float)std::max<int64_t>(1, obs_data_get_int(settings, "viz_height"));
	viz.geometry.opacity = (float)obs_data_get_double(settings, "viz_opacity") / 100.0f;
	viz.geometry.z = (int)obs_data_get_int(settings, "viz_z");

	viz.mode = (VizMode)obs_data_get_int(settings, "viz_mode");
	viz.bands = (int)std::clamp<int64_t>(obs_data_get_int(settings, "viz_bands"), 4, 128);
	viz.gap = (float)obs_data_get_double(settings, "viz_gap");
	viz.rounded = obs_data_get_bool(settings, "viz_rounded");
	viz.line_width = (float)obs_data_get_double(settings, "viz_line_width");
	viz.ring_inner = (float)obs_data_get_double(settings, "viz_ring_inner");
	viz.color = (uint32_t)obs_data_get_int(settings, "viz_color");
	viz.color2 = (uint32_t)obs_data_get_int(settings, "viz_color2");
	viz.gradient = obs_data_get_bool(settings, "viz_gradient");
	viz.follow_accent = obs_data_get_bool(settings, "viz_accent");
	viz.tone = (AccentTone)obs_data_get_int(settings, "viz_tone");
	viz.sensitivity = (float)obs_data_get_double(settings, "viz_sensitivity");
	viz.smoothing = (float)obs_data_get_double(settings, "viz_smoothing");
	viz.min_freq = (float)obs_data_get_double(settings, "viz_min_freq");
	viz.max_freq = (float)obs_data_get_double(settings, "viz_max_freq");
}

void tick_visualizer(MusicWidget *widget, float seconds, bool playing, bool preview)
{
	VisualizerElement &viz = widget->viz;

	attach_audio(widget, viz.geometry.enabled && !preview);

	if (!viz.geometry.enabled)
		return;

	viz.time += seconds;

	const bool wave = viz.mode == VizMode::Wave;
	const int count = wave ? std::clamp(viz.bands * 3, 16, 256) : viz.bands;

	std::vector<float> target;

	if (preview) {
		if (wave)
			synthetic_wave(viz, count, target);
		else
			synthetic_levels(viz, count, target);
	} else {
		static thread_local std::vector<float> samples;
		const uint64_t frames = copy_ring(widget->tap, samples);

		if (frames != viz.last_frames) {
			viz.last_frames = frames;
			viz.stale = 0.0f;
		} else {
			viz.stale += seconds;
		}

		const bool live = playing && viz.stale < STALE_SECONDS;
		const float gain = std::max(0.0f, viz.sensitivity / 100.0f);

		if (!live)
			target.assign((size_t)count, 0.0f);
		else if (wave)
			wave_points(samples.data(), samples.size(), count, gain, target);
		else
			widget->spectrum.analyse(samples.data(), samples.size(), widget->tap.sample_rate, count,
						 viz.min_freq, viz.max_freq, gain, target);
	}

	if (wave) {
		/* A light touch only: a waveform smoothed too far stops looking
		 * like one. */
		if (viz.wave.size() != target.size())
			viz.wave.assign(target.size(), 0.0f);
		const float rate = 1.0f - std::exp(-seconds * 30.0f);
		for (size_t i = 0; i < target.size(); i++)
			viz.wave[i] += (target[i] - viz.wave[i]) * rate;
	} else {
		smooth_levels(viz.levels, target, seconds, viz.smoothing / 100.0f);
	}
}

void detach_audio(MusicWidget *widget)
{
	attach_audio(widget, false);
}

void draw_visualizer(MusicWidget *widget)
{
	const VisualizerElement &viz = widget->viz;

	if (!viz.geometry.enabled)
		return;

	float x;
	float y;
	resolve_position(viz.geometry, widget->width, widget->height, x, y);

	const float width = viz.geometry.width;
	const float height = viz.geometry.height;

	const uint32_t color = resolve_color(widget, viz.color, viz.follow_accent, viz.tone);

	DrawParams base;
	base.fill_mode = viz.gradient ? 1 : 0;
	base.color = color;
	base.color2 = viz.gradient ? viz.color2 : color;
	base.gradient_angle = 270.0f; /* the first colour at the foot of each bar */
	base.opacity = viz.geometry.opacity * widget->draw_opacity;

	const float gap = std::clamp(viz.gap, 0.0f, 90.0f) / 100.0f;

	if (viz.mode == VizMode::Wave) {
		const size_t points = viz.wave.size();
		if (points < 2)
			return;

		const float thickness = std::max(1.0f, viz.line_width);
		const float mid = y + height * 0.5f;
		const float reach = std::max(0.0f, height * 0.5f - thickness * 0.5f);

		for (size_t i = 0; i + 1 < points; i++) {
			const float x0 = x + width * (float)i / (float)(points - 1);
			const float x1 = x + width * (float)(i + 1) / (float)(points - 1);
			const float y0 = mid - viz.wave[i] * reach;
			const float y1 = mid - viz.wave[i + 1] * reach;

			const float dx = x1 - x0;
			const float dy = y1 - y0;
			const float length = std::sqrt(dx * dx + dy * dy) + thickness * 0.5f;

			DrawParams params = base;
			params.fill_mode = 0;
			params.width = length;
			params.height = thickness;
			params.x = (x0 + x1) * 0.5f - length * 0.5f;
			params.y = (y0 + y1) * 0.5f - thickness * 0.5f;
			params.rotation = std::atan2(dy, dx) * 180.0f / PI;
			params.corner_radius = viz.rounded ? thickness * 0.5f : 0.0f;
			draw_quad(widget->effect, params, false);
		}
		return;
	}

	const size_t bands = viz.levels.size();
	if (!bands)
		return;

	if (viz.mode == VizMode::Ring) {
		const float cx = x + width * 0.5f;
		const float cy = y + height * 0.5f;
		const float outer = std::min(width, height) * 0.5f;
		const float inner = outer * std::clamp(viz.ring_inner, 5.0f, 95.0f) / 100.0f;
		const float reach = outer - inner;
		const float thickness = std::max(1.0f, 2.0f * PI * inner / (float)bands * (1.0f - gap));

		for (size_t i = 0; i < bands; i++) {
			const float angle = 2.0f * PI * (float)i / (float)bands - PI * 0.5f;
			const float length = std::max(thickness, viz.levels[i] * reach);
			const float centre = inner + length * 0.5f;

			DrawParams params = base;
			params.gradient_angle = 0.0f; /* outwards along the bar */
			params.width = length;
			params.height = thickness;
			params.x = cx + std::cos(angle) * centre - length * 0.5f;
			params.y = cy + std::sin(angle) * centre - thickness * 0.5f;
			params.rotation = angle * 180.0f / PI;
			params.corner_radius = viz.rounded ? thickness * 0.5f : 0.0f;
			draw_quad(widget->effect, params, false);
		}
		return;
	}

	const float slot = width / (float)bands;
	const float bar = std::max(1.0f, slot * (1.0f - gap));
	const bool mirror = viz.mode == VizMode::Mirror;

	for (size_t i = 0; i < bands; i++) {
		const float floor = std::min(bar, height);
		const float level = std::max(floor, viz.levels[i] * height);

		DrawParams params = base;
		params.width = bar;
		params.height = level;
		params.x = x + slot * (float)i + (slot - bar) * 0.5f;
		params.y = mirror ? y + (height - level) * 0.5f : y + height - level;
		params.corner_radius = viz.rounded ? bar * 0.5f : 0.0f;
		if (mirror)
			params.gradient_angle = 90.0f;
		draw_quad(widget->effect, params, false);
	}
}

} // namespace vr::widget
