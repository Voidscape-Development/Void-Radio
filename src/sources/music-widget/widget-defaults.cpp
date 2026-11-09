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

/* The widget's default settings. */

#include "sources/music-widget/widget-design.hpp"
#include "sources/music-widget/widget.hpp"

namespace vr::widget {

/* ------------------------------------------------------------------------- */
/* Defaults                                                                   */
/* ------------------------------------------------------------------------- */

static void set_geometry_defaults(obs_data_t *settings, const std::string &prefix, bool enabled, Anchor anchor,
				  double x, double y, int64_t width, int64_t height, int64_t z)
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

static void set_font_default(obs_data_t *settings, const std::string &prefix, const char *face, int64_t size, bool bold)
{
	obs_data_t *font = obs_data_create();

	obs_data_set_string(font, "face", face);
	obs_data_set_string(font, "style", bold ? "Bold" : "Regular");
	obs_data_set_int(font, "size", size);
	obs_data_set_int(font, "flags", bold ? OBS_FONT_BOLD : 0);

	obs_data_set_default_obj(settings, key_for(prefix, "font").c_str(), font);

	obs_data_release(font);
}

static void set_text_defaults(obs_data_t *settings, int slot)
{
	const std::string prefix = text_prefix(slot);

	/* Only the first two slots are switched on out of the box: a title and
	 * an artist is what a fresh widget should look like. */
	const bool enabled = slot < 2;

	static const char *const FORMATS[TEXT_SLOTS] = {
		"{title}", "{artist}", "{album}[ · {year}]", "{elapsed} / {duration}", "", ""};
	obs_data_set_default_string(settings, key_for(prefix, "format").c_str(), FORMATS[slot]);
	obs_data_set_default_string(settings, key_for(prefix, "idle_format").c_str(), slot == 0 ? "{state}" : "");
	obs_data_set_default_bool(settings, key_for(prefix, "hide_empty").c_str(), true);
	obs_data_set_default_bool(settings, key_for(prefix, "accent").c_str(), false);
	obs_data_set_default_int(settings, key_for(prefix, "tone").c_str(), (int64_t)AccentTone::Accent);
	obs_data_set_default_bool(settings, key_for(prefix, "shadow_accent").c_str(), false);
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

/* The extras are all off out of the box. */
static void box_defaults(obs_data_t *settings, const std::string &prefix, Anchor anchor, double x, double y,
			 int64_t width, int64_t height, int64_t z)
{
	obs_data_set_default_bool(settings, key_for(prefix, "enable").c_str(), false);
	obs_data_set_default_int(settings, key_for(prefix, "anchor").c_str(), (int64_t)anchor);
	obs_data_set_default_double(settings, key_for(prefix, "x").c_str(), x);
	obs_data_set_default_double(settings, key_for(prefix, "y").c_str(), y);
	obs_data_set_default_int(settings, key_for(prefix, "width").c_str(), width);
	obs_data_set_default_int(settings, key_for(prefix, "height").c_str(), height);
	obs_data_set_default_double(settings, key_for(prefix, "opacity").c_str(), 100.0);
	obs_data_set_default_int(settings, key_for(prefix, "z").c_str(), z);
}

static void add_extras_defaults(obs_data_t *settings)
{
	box_defaults(settings, "img_", Anchor::TopRight, -16.0, 16.0, 96, 48, 30);
	obs_data_set_default_int(settings, "img_fit", (int64_t)ArtFit::Contain);
	obs_data_set_default_double(settings, "img_corner_radius", 0.0);
	obs_data_set_default_string(settings, "img_path", "");

	box_defaults(settings, "icon_", Anchor::TopRight, -16.0, 16.0, 28, 28, 30);
	obs_data_set_default_int(settings, "icon_color", 0xFFFFFFFF);
	obs_data_set_default_bool(settings, "icon_accent", false);
	obs_data_set_default_int(settings, "icon_tone", (int64_t)AccentTone::Accent);
	obs_data_set_default_int(settings, "icon_show", (int64_t)IconShow::Always);

	/* Sits behind the art: art defaults to z 0. */
	box_defaults(settings, "glow_", Anchor::MiddleLeft, 0.0, 0.0, 180, 180, -10);
	obs_data_set_default_double(settings, "glow_opacity", 80.0);
	obs_data_set_default_double(settings, "glow_corner_radius", 32.0);
	obs_data_set_default_double(settings, "glow_softness", 20.0);
}

static void add_visualizer_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "viz_enable", false);
	obs_data_set_default_int(settings, "viz_anchor", (int64_t)Anchor::BottomRight);
	obs_data_set_default_double(settings, "viz_x", -16.0);
	obs_data_set_default_double(settings, "viz_y", -16.0);
	obs_data_set_default_int(settings, "viz_width", 160);
	obs_data_set_default_int(settings, "viz_height", 48);
	obs_data_set_default_double(settings, "viz_opacity", 100.0);
	obs_data_set_default_int(settings, "viz_z", 5);

	obs_data_set_default_int(settings, "viz_mode", (int64_t)VizMode::Bars);
	obs_data_set_default_int(settings, "viz_bands", 24);
	obs_data_set_default_double(settings, "viz_gap", 35.0);
	obs_data_set_default_bool(settings, "viz_rounded", true);
	obs_data_set_default_double(settings, "viz_line_width", 3.0);
	obs_data_set_default_double(settings, "viz_ring_inner", 72.0);
	obs_data_set_default_int(settings, "viz_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "viz_color2", 0xFFFFFFFF);
	obs_data_set_default_bool(settings, "viz_gradient", false);
	obs_data_set_default_bool(settings, "viz_accent", true);
	obs_data_set_default_int(settings, "viz_tone", (int64_t)AccentTone::Accent);
	obs_data_set_default_double(settings, "viz_sensitivity", 100.0);
	obs_data_set_default_double(settings, "viz_smoothing", 60.0);
	obs_data_set_default_double(settings, "viz_min_freq", 40.0);
	obs_data_set_default_double(settings, "viz_max_freq", 14000.0);
}

static void add_motion_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "popup_mode", (int64_t)PopupMode::Always);
	obs_data_set_default_int(settings, "popup_animation", (int64_t)PopupAnimation::SlideUp);
	obs_data_set_default_double(settings, "popup_hold", 8.0);
	obs_data_set_default_int(settings, "popup_ms", 450);

	obs_data_set_default_int(settings, "enter_style", (int64_t)EnterStyle::None);
	obs_data_set_default_int(settings, "enter_ms", 450);
	obs_data_set_default_int(settings, "enter_stagger_ms", 80);
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

	obs_data_set_default_bool(settings, "bg_enable", true);
	obs_data_set_default_int(settings, "bg_type", (int64_t)BackgroundType::Color);
	obs_data_set_default_int(settings, "bg_color", 0xC0141414);
	obs_data_set_default_int(settings, "bg_color2", 0xC0303030);
	obs_data_set_default_double(settings, "bg_gradient_angle", 90.0);
	obs_data_set_default_bool(settings, "bg_accent", false);
	obs_data_set_default_int(settings, "bg_tone", (int64_t)AccentTone::Accent);
	obs_data_set_default_bool(settings, "bg_accent2", false);
	obs_data_set_default_int(settings, "bg_tone2", (int64_t)AccentTone::Dark);
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

	add_extras_defaults(settings);
	add_visualizer_defaults(settings);
	add_motion_defaults(settings);

	add_design_defaults(settings);
}

} // namespace vr::widget
