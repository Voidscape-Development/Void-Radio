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

/* Movement that is about time rather than about the music: the card showing
 * itself for a while when a track starts, and each element arriving in turn. */

#include "sources/music-widget/widget.hpp"

#include <algorithm>
#include <cmath>

namespace vr::widget {

namespace {

float ease_out(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	const float inverse = 1.0f - t;
	return 1.0f - inverse * inverse * inverse;
}

float smoothstep(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

void popup_hotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		static_cast<MusicWidget *>(data)->popup_request = true;
}

/* 0 hidden .. 1 shown, before easing, for the time since it was asked. */
float popup_phase(const MusicWidget *widget)
{
	if (widget->popup_mode == PopupMode::Always)
		return 1.0f;

	const float edge = std::max(0.01f, widget->popup_seconds);
	const float t = widget->popup_time;

	if (t < edge)
		return t / edge;
	if (t < edge + widget->popup_hold)
		return 1.0f;
	if (t < edge * 2.0f + widget->popup_hold)
		return 1.0f - (t - edge - widget->popup_hold) / edge;
	return 0.0f;
}

} // namespace

void read_motion(MusicWidget *widget, obs_data_t *settings)
{
	const PopupMode mode = (PopupMode)obs_data_get_int(settings, "popup_mode");

	/* Switching to "on track change" shows the card once, so the change is
	 * visible instead of the card simply vanishing. */
	if (mode != widget->popup_mode && mode == PopupMode::OnTrackChange)
		widget->popup_time = 0.0f;

	widget->popup_mode = mode;
	widget->popup_animation = (PopupAnimation)obs_data_get_int(settings, "popup_animation");
	widget->popup_hold = std::max(0.5f, (float)obs_data_get_double(settings, "popup_hold"));
	widget->popup_seconds = (float)obs_data_get_int(settings, "popup_ms") / 1000.0f;

	widget->enter_style = (EnterStyle)obs_data_get_int(settings, "enter_style");
	widget->enter_seconds = std::max(0.05f, (float)obs_data_get_int(settings, "enter_ms") / 1000.0f);
	widget->enter_stagger = (float)obs_data_get_int(settings, "enter_stagger_ms") / 1000.0f;
}

void register_motion_hotkey(MusicWidget *widget)
{
	widget->popup_hotkey = obs_hotkey_register_source(widget->self, "void_radio.widget.show",
							  obs_module_text("Widget.Hotkey.Show"), popup_hotkey, widget);
}

void unregister_motion_hotkey(MusicWidget *widget)
{
	if (widget->popup_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(widget->popup_hotkey);
	widget->popup_hotkey = OBS_INVALID_HOTKEY_ID;
}

void trigger_popup(MusicWidget *widget)
{
	if (widget->popup_mode != PopupMode::OnTrackChange)
		return;

	const float edge = std::max(0.01f, widget->popup_seconds);
	const float phase = popup_phase(widget);

	if (phase <= 0.0f) {
		/* Hidden: come in from the start, with the elements arriving. */
		widget->popup_time = 0.0f;
		trigger_entrance(widget);
	} else if (widget->popup_time >= edge + widget->popup_hold) {
		/* On the way out: turn round from wherever it has got to. */
		widget->popup_time = edge * phase;
	} else if (widget->popup_time >= edge) {
		/* Already shown: just stay up for longer. */
		widget->popup_time = edge;
	}
}

void trigger_entrance(MusicWidget *widget)
{
	if (widget->enter_style != EnterStyle::None)
		widget->enter_time = 0.0f;
}

void tick_motion(MusicWidget *widget, float seconds)
{
	if (widget->popup_request.exchange(false)) {
		if (widget->popup_mode == PopupMode::OnTrackChange)
			trigger_popup(widget);
		else
			trigger_entrance(widget);
	}

	widget->popup_time = std::min(widget->popup_time + seconds, 1e9f);
	widget->enter_time = std::min(widget->enter_time + seconds, 1e9f);

	widget->popup_level = smoothstep(popup_phase(widget));
}

Reveal element_reveal(const MusicWidget *widget, int order)
{
	Reveal reveal;

	if (widget->enter_style == EnterStyle::None || order < 0)
		return reveal;

	const float t = (widget->enter_time - (float)order * widget->enter_stagger) / widget->enter_seconds;
	if (t >= 1.0f)
		return reveal;

	const float eased = ease_out(t);
	reveal.opacity = std::clamp(t * 1.4f, 0.0f, 1.0f);

	switch (widget->enter_style) {
	case EnterStyle::SlideUp:
		reveal.dy = (1.0f - eased) * 22.0f;
		break;
	case EnterStyle::SlideLeft:
		reveal.dx = (1.0f - eased) * 36.0f;
		break;
	case EnterStyle::Scale:
		reveal.scale = 0.6f + 0.4f * eased;
		break;
	default:
		break;
	}

	return reveal;
}

Reveal popup_reveal(const MusicWidget *widget)
{
	Reveal reveal;

	if (widget->popup_mode == PopupMode::Always)
		return reveal;

	const float level = widget->popup_level;
	const float away = 1.0f - level;
	reveal.opacity = level;

	switch (widget->popup_animation) {
	case PopupAnimation::SlideUp:
		reveal.dy = away * (float)widget->height;
		break;
	case PopupAnimation::SlideDown:
		reveal.dy = -away * (float)widget->height;
		break;
	case PopupAnimation::SlideLeft:
		reveal.dx = away * (float)widget->width;
		break;
	case PopupAnimation::SlideRight:
		reveal.dx = -away * (float)widget->width;
		break;
	case PopupAnimation::Scale:
		reveal.scale = 0.8f + 0.2f * level;
		break;
	default:
		break;
	}

	return reveal;
}

} // namespace vr::widget
