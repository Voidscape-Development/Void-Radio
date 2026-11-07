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

/* Reading the widget's settings, its defaults, and the layout presets. */

#include "sources/music-widget/widget.hpp"

#include <plugin-support.h>

namespace vr::widget {

/* ------------------------------------------------------------------------- */
/* Settings                                                                   */
/* ------------------------------------------------------------------------- */

static void read_geometry(Geometry &geometry, obs_data_t *settings, const std::string &prefix, bool read_size)
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

static void read_text_style(TextStyle &style, obs_data_t *settings, const std::string &prefix)
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

static void load_background_image(BackgroundElement &background, const char *path)
{
	const std::string next = path ? path : "";

	if (background.image_valid && next == background.image_path)
		return;

	if (background.image_valid) {
		obs_enter_graphics();
		vr_image_file_free(&background.image);
		obs_leave_graphics();
		background.image_valid = false;
	}

	background.image_path = next;

	if (background.image_path.empty())
		return;

	vr_image_file_init(&background.image, background.image_path.c_str(), GS_IMAGE_ALPHA_STRAIGHT);

	obs_enter_graphics();
	vr_image_file_init_texture(&background.image);
	obs_leave_graphics();

	background.image_valid = vr_image_file_base(background.image).loaded;

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

static void write_geometry(obs_data_t *settings, const std::string &prefix, bool enabled, Anchor anchor, double x,
			   double y, int64_t width, int64_t height)
{
	obs_data_set_bool(settings, key_for(prefix, "enable").c_str(), enabled);
	obs_data_set_int(settings, key_for(prefix, "anchor").c_str(), (int64_t)anchor);
	obs_data_set_double(settings, key_for(prefix, "x").c_str(), x);
	obs_data_set_double(settings, key_for(prefix, "y").c_str(), y);
	obs_data_set_int(settings, key_for(prefix, "width").c_str(), width);
	obs_data_set_int(settings, key_for(prefix, "height").c_str(), height);
}

static void write_font(obs_data_t *settings, const std::string &prefix, int64_t size, bool bold)
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

} // namespace vr::widget
