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

/* Reading the widget's settings into its elements. */

#include "sources/music-widget/widget-design.hpp"
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

void widget_update(void *data, obs_data_t *raw_settings)
{
	MusicWidget *widget = static_cast<MusicWidget *>(data);

	/* The music source binding writes back into the real settings. */
	widget->link.update(raw_settings);

	/* Everything else reads the settings with the layout and style worked
	 * into each element, exactly as if those values had been set by hand. */
	obs_data_t *settings = resolve_settings(raw_settings);

	widget->fixed_accent = (ColorSource)obs_data_get_int(settings, "color_source") == ColorSource::Fixed;
	widget->fixed_color = (uint32_t)obs_data_get_int(settings, "fixed_color");

	widget->width = (uint32_t)std::max<int64_t>(1, obs_data_get_int(settings, "width"));
	widget->height = (uint32_t)std::max<int64_t>(1, obs_data_get_int(settings, "height"));
	widget->time_format = (TimeFormat)obs_data_get_int(settings, "time_format");

	widget->transition = (TrackTransition)obs_data_get_int(settings, "transition");
	widget->transition_seconds = (float)obs_data_get_int(settings, "transition_ms") / 1000.0f;
	widget->idle_mode = (WidgetIdle)obs_data_get_int(settings, "idle_mode");
	widget->idle_trigger = (WidgetIdleTrigger)obs_data_get_int(settings, "idle_trigger");

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

	obs_data_release(settings);
}

} // namespace vr::widget
