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

/* The widget's properties dialog.
 *
 * The main view asks three things - how the widget is laid out, how it looks,
 * and what its lines say - and everything else waits behind "Show advanced
 * options". Groups that the chosen layout or style already decide are hidden,
 * so nothing on screen is a setting that does nothing. */

#include "sources/music-widget/widget-design.hpp"
#include "sources/music-widget/widget.hpp"

#ifdef VOID_RADIO_ENABLE_EDITOR
#include "ui/widget-editor.hpp"
#endif

namespace vr::widget {

namespace {

const char *const LINE_NAMES[TEXT_SLOTS] = {"Widget.Line1", "Widget.Line2", "Widget.Line3",
					    "Widget.Line4", "Widget.Line5", "Widget.Line6"};
const char *const LINE_DETAILS[TEXT_SLOTS] = {"Widget.Line1.Details", "Widget.Line2.Details", "Widget.Line3.Details",
					      "Widget.Line4.Details", "Widget.Line5.Details", "Widget.Line6.Details"};
const char *const LINE_STYLE[TEXT_SLOTS] = {"Widget.Line1.Style", "Widget.Line2.Style", "Widget.Line3.Style",
					    "Widget.Line4.Style", "Widget.Line5.Style", "Widget.Line6.Style"};

const char *const GEOMETRY_KEYS[] = {"anchor", "x", "y", "width", "height", "opacity", "z"};

void set_visible(obs_properties_t *props, const std::string &name, bool visible)
{
	if (obs_property_t *property = obs_properties_get(props, name.c_str()))
		obs_property_set_visible(property, visible);
}

/* Every visibility rule in one place, run whenever anything they depend on
 * changes. */
bool refresh_properties(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	const bool free_layout = (Arrangement)obs_data_get_int(settings, "layout") == Arrangement::Free;
	const bool custom_style = (WidgetStyle)obs_data_get_int(settings, "style") == WidgetStyle::Custom;
	const bool advanced = obs_data_get_bool(settings, "show_advanced");

	/* Main view. */
	set_visible(props, "fixed_color",
		    (ColorSource)obs_data_get_int(settings, "color_source") == ColorSource::Fixed);
	set_visible(props, "font", !custom_style);
	set_visible(props, "text_scale", !free_layout);
	set_visible(props, "bar_position", !free_layout);
	set_visible(props, "long_text", !custom_style);

	/* Advanced: fine tuning. */
	set_visible(props, "tuning_group", advanced);
	set_visible(props, "spacing", !free_layout);
	set_visible(props, "art_scale", !free_layout);
	set_visible(props, "radius", obs_data_get_bool(settings, "radius_override"));

	const ArtAnimation animation = (ArtAnimation)obs_data_get_int(settings, "art_animation");
	set_visible(props, "art_spin_rpm", animation == ArtAnimation::Spin);
	set_visible(props, "art_pulse_percent", animation == ArtAnimation::Pulse);
	set_visible(props, "art_pulse_rate", animation == ArtAnimation::Pulse);

	/* Lines. */
	for (int slot = 0; slot < TEXT_SLOTS; slot++) {
		const std::string prefix = text_prefix(slot);
		const bool own_style = custom_style || obs_data_get_bool(settings, key_for(prefix, "custom").c_str());

		if (slot >= MAIN_LINES)
			set_visible(props, key_for(prefix, "enable"), advanced);

		set_visible(props, key_for(prefix, "details"), advanced);
		set_visible(props, key_for(prefix, "custom"), !custom_style);
		for (const char *key : GEOMETRY_KEYS)
			set_visible(props, key_for(prefix, key), free_layout);

		set_visible(props, key_for(prefix, "style"), advanced && own_style);

		const TextOverflow overflow =
			(TextOverflow)obs_data_get_int(settings, key_for(prefix, "overflow").c_str());
		const bool marquee = overflow == TextOverflow::Marquee;
		set_visible(props, key_for(prefix, "marquee_style"), marquee);
		set_visible(props, key_for(prefix, "marquee_speed"), marquee);
		set_visible(props, key_for(prefix, "marquee_gap"), marquee);
		set_visible(props, key_for(prefix, "marquee_pause"), marquee);
		set_visible(props, key_for(prefix, "ellipsis_side"), overflow == TextOverflow::Ellipsis);
		set_visible(props, key_for(prefix, "max_lines"), overflow == TextOverflow::Wrap);
		set_visible(props, key_for(prefix, "shrink_min"), overflow == TextOverflow::Shrink);
	}

	/* Elements: positions only under Free placement, looks only under the
	 * Custom style. */
	set_visible(props, "art_details", advanced && free_layout);
	set_visible(props, "art_style", advanced && custom_style);
	set_visible(props, "bg_style", advanced && custom_style);
	set_visible(props, "bar_details", advanced && free_layout);
	set_visible(props, "bar_style", advanced && custom_style);

	const BackgroundType type = (BackgroundType)obs_data_get_int(settings, "bg_type");
	set_visible(props, "bg_color", type != BackgroundType::Image && type != BackgroundType::BlurredArt);
	set_visible(props, "bg_accent", type == BackgroundType::Color || type == BackgroundType::Gradient);
	set_visible(props, "bg_color2", type == BackgroundType::Gradient);
	set_visible(props, "bg_gradient_angle", type == BackgroundType::Gradient);
	set_visible(props, "bg_image", type == BackgroundType::Image);
	set_visible(props, "bg_blur", type == BackgroundType::BlurredArt);
	set_visible(props, "bg_darken", type == BackgroundType::BlurredArt);
	set_visible(props, "bg_desaturate", type == BackgroundType::BlurredArt);

	return true;
}

#ifdef VOID_RADIO_ENABLE_EDITOR
bool open_editor_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	MusicWidget *widget = static_cast<MusicWidget *>(data);
	if (widget && widget->self)
		open_widget_editor(widget->self);

	return false;
}
#endif

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

void add_background_properties(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();

	obs_properties_add_bool(group, "bg_enable", obs_module_text("Widget.Visible"));

	obs_property_t *type = obs_properties_add_list(group, "bg_type", obs_module_text("Widget.Background.Type"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Color"), (int64_t)BackgroundType::Color);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Gradient"), (int64_t)BackgroundType::Gradient);
	obs_property_list_add_int(type, obs_module_text("Bar.FillType.Image"), (int64_t)BackgroundType::Image);
	obs_property_list_add_int(type, obs_module_text("Widget.Background.BlurredArt"),
				  (int64_t)BackgroundType::BlurredArt);
	obs_property_set_modified_callback(type, refresh_properties);

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

	obs_properties_add_group(props, "bg_style", obs_module_text("Widget.BackgroundStyle"), OBS_GROUP_NORMAL, group);
}

void add_art_properties(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();

	obs_properties_add_bool(group, "art_enable", obs_module_text("Widget.Visible"));
	add_geometry_properties(group, "art_");
	obs_properties_add_group(props, "art_details", obs_module_text("Widget.ArtPosition"), OBS_GROUP_NORMAL, group);

	group = obs_properties_create();

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

	obs_properties_add_group(props, "art_style", obs_module_text("Widget.ArtStyle"), OBS_GROUP_NORMAL, group);
}

void add_line_details(obs_properties_t *props, int slot)
{
	const std::string prefix = text_prefix(slot);

	obs_properties_t *group = obs_properties_create();

	obs_properties_add_text(group, key_for(prefix, "idle_format").c_str(), obs_module_text("Info.IdleFormat"),
				OBS_TEXT_MULTILINE);

	obs_property_t *hide_empty = obs_properties_add_bool(group, key_for(prefix, "hide_empty").c_str(),
							     obs_module_text("Widget.Text.HideEmpty"));
	obs_property_set_long_description(hide_empty, obs_module_text("Widget.Text.HideEmpty.Description"));

	obs_property_t *role = obs_properties_add_list(group, key_for(prefix, "role").c_str(),
						       obs_module_text("Widget.Line.Role"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(role, obs_module_text("Widget.Line.Role.Title"), (int64_t)LineRole::Title);
	obs_property_list_add_int(role, obs_module_text("Widget.Line.Role.Subtitle"), (int64_t)LineRole::Subtitle);
	obs_property_list_add_int(role, obs_module_text("Widget.Line.Role.Detail"), (int64_t)LineRole::Detail);
	obs_property_list_add_int(role, obs_module_text("Widget.Line.Role.Time"), (int64_t)LineRole::Time);
	obs_property_set_long_description(role, obs_module_text("Widget.Line.Role.Description"));

	obs_property_t *custom = obs_properties_add_bool(group, key_for(prefix, "custom").c_str(),
							 obs_module_text("Widget.Line.Custom"));
	obs_property_set_long_description(custom, obs_module_text("Widget.Line.Custom.Description"));
	obs_property_set_modified_callback(custom, refresh_properties);

	add_geometry_properties(group, prefix);

	obs_properties_add_group(props, key_for(prefix, "details").c_str(), obs_module_text(LINE_DETAILS[slot]),
				 OBS_GROUP_NORMAL, group);

	group = obs_properties_create();

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
	obs_property_set_modified_callback(overflow, refresh_properties);

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

	obs_properties_add_group(props, key_for(prefix, "style").c_str(), obs_module_text(LINE_STYLE[slot]),
				 OBS_GROUP_NORMAL, group);
}

void add_line_main(obs_properties_t *props, int slot)
{
	const std::string prefix = text_prefix(slot);
	obs_properties_t *group = obs_properties_create();

	obs_property_t *format = obs_properties_add_text(group, key_for(prefix, "format").c_str(),
							 obs_module_text("Widget.LineText"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(format, obs_module_text("Info.Fields"));

	obs_properties_add_group(props, key_for(prefix, "enable").c_str(), obs_module_text(LINE_NAMES[slot]),
				 OBS_GROUP_CHECKABLE, group);
}

void add_list_item(obs_property_t *list, const char *text, int64_t value)
{
	obs_property_list_add_int(list, obs_module_text(text), value);
}

obs_property_t *add_list(obs_properties_t *props, const char *name, const char *text)
{
	return obs_properties_add_list(props, name, obs_module_text(text), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
}

void add_layout_group(obs_properties_t *props, void *data)
{
	obs_properties_t *group = obs_properties_create();

	obs_property_t *layout = add_list(group, "layout", "Widget.Layout");
	add_list_item(layout, "Widget.Layout.ArtLeft", (int64_t)Arrangement::ArtLeft);
	add_list_item(layout, "Widget.Layout.ArtRight", (int64_t)Arrangement::ArtRight);
	add_list_item(layout, "Widget.Layout.Stacked", (int64_t)Arrangement::Stacked);
	add_list_item(layout, "Widget.Layout.Backdrop", (int64_t)Arrangement::Backdrop);
	add_list_item(layout, "Widget.Layout.Compact", (int64_t)Arrangement::Compact);
	add_list_item(layout, "Widget.Layout.TextOnly", (int64_t)Arrangement::TextOnly);
	add_list_item(layout, "Widget.Layout.Free", (int64_t)Arrangement::Free);
	obs_property_set_long_description(layout, obs_module_text("Widget.Layout.Description"));
	obs_property_set_modified_callback(layout, refresh_properties);

	obs_properties_add_int(group, "width", obs_module_text("Widget.CanvasWidth"), 16, 8192, 1);
	obs_properties_add_int(group, "height", obs_module_text("Widget.CanvasHeight"), 16, 8192, 1);

#ifdef VOID_RADIO_ENABLE_EDITOR
	obs_properties_add_button2(group, "open_editor", obs_module_text("Widget.OpenEditor"), open_editor_clicked,
				   data);
#else
	UNUSED_PARAMETER(data);
#endif

	obs_properties_add_group(props, "layout_group", obs_module_text("Widget.Group.Layout"), OBS_GROUP_NORMAL,
				 group);
}

void add_style_group(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();

	obs_property_t *style = add_list(group, "style", "Widget.Style");
	add_list_item(style, "Widget.Style.Glass", (int64_t)WidgetStyle::Glass);
	add_list_item(style, "Widget.Style.Solid", (int64_t)WidgetStyle::Solid);
	add_list_item(style, "Widget.Style.Minimal", (int64_t)WidgetStyle::Minimal);
	add_list_item(style, "Widget.Style.Outline", (int64_t)WidgetStyle::Outline);
	add_list_item(style, "Widget.Style.Vinyl", (int64_t)WidgetStyle::Vinyl);
	add_list_item(style, "Widget.Style.Custom", (int64_t)WidgetStyle::Custom);
	obs_property_set_long_description(style, obs_module_text("Widget.Style.Description"));
	obs_property_set_modified_callback(style, refresh_properties);

	obs_property_t *source = add_list(group, "color_source", "Widget.ColorSource");
	add_list_item(source, "Widget.ColorSource.Album", (int64_t)ColorSource::Album);
	add_list_item(source, "Widget.ColorSource.Fixed", (int64_t)ColorSource::Fixed);
	obs_property_set_long_description(source, obs_module_text("Widget.ColorSource.Description"));
	obs_property_set_modified_callback(source, refresh_properties);

	obs_properties_add_color_alpha(group, "fixed_color", obs_module_text("Widget.FixedColor"));

	obs_property_t *font = obs_properties_add_font(group, "font", obs_module_text("Widget.Font"));
	obs_property_set_long_description(font, obs_module_text("Widget.Font.Description"));

	obs_property_t *scale =
		obs_properties_add_int_slider(group, "text_scale", obs_module_text("Widget.TextScale"), 50, 200, 5);
	obs_property_int_set_suffix(scale, "%");

	obs_properties_add_group(props, "style_group", obs_module_text("Widget.Group.Style"), OBS_GROUP_NORMAL, group);
}

void add_text_group(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();

	obs_property_t *long_text = add_list(group, "long_text", "Widget.LongText");
	add_list_item(long_text, "Widget.Text.Overflow.Marquee", (int64_t)LongText::Scroll);
	add_list_item(long_text, "Widget.Text.Overflow.Shrink", (int64_t)LongText::Shrink);
	add_list_item(long_text, "Widget.Text.Overflow.Ellipsis", (int64_t)LongText::Truncate);

	obs_property_t *bar = add_list(group, "bar_position", "Widget.BarPosition");
	add_list_item(bar, "Widget.BarPosition.UnderText", (int64_t)BarPlacement::UnderText);
	add_list_item(bar, "Widget.BarPosition.BottomEdge", (int64_t)BarPlacement::BottomEdge);
	add_list_item(bar, "Widget.BarPosition.Off", (int64_t)BarPlacement::Off);

	obs_property_t *idle = add_list(group, "idle_mode", "Widget.Idle");
	add_list_item(idle, "Widget.Idle.Hide", (int64_t)WidgetIdle::Hide);
	add_list_item(idle, "Widget.Idle.Placeholder", (int64_t)WidgetIdle::Placeholder);
	add_list_item(idle, "Widget.Idle.Freeze", (int64_t)WidgetIdle::Freeze);
	obs_property_set_long_description(idle, obs_module_text("Widget.Idle.Description"));

	obs_property_t *transition = add_list(group, "transition", "Widget.Transition");
	add_list_item(transition, "Widget.Transition.None", (int64_t)TrackTransition::None);
	add_list_item(transition, "Widget.Transition.CrossFade", (int64_t)TrackTransition::CrossFade);
	add_list_item(transition, "Widget.Transition.Slide", (int64_t)TrackTransition::Slide);
	add_list_item(transition, "Widget.Transition.FadeThroughBlank", (int64_t)TrackTransition::FadeThroughBlank);

	obs_properties_add_group(props, "text_group", obs_module_text("Widget.Group.Text"), OBS_GROUP_NORMAL, group);
}

void add_tuning_group(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();

	obs_property_t *spacing =
		obs_properties_add_int_slider(group, "spacing", obs_module_text("Widget.Spacing"), 0, 250, 5);
	obs_property_int_set_suffix(spacing, "%");

	obs_property_t *art_scale =
		obs_properties_add_int_slider(group, "art_scale", obs_module_text("Widget.ArtScale"), 20, 100, 5);
	obs_property_int_set_suffix(art_scale, "%");

	obs_property_t *shape = add_list(group, "art_shape", "Widget.ArtShape");
	add_list_item(shape, "Widget.ArtShape.FromStyle", (int64_t)ArtShape::FromStyle);
	add_list_item(shape, "Widget.ArtShape.Square", (int64_t)ArtShape::Square);
	add_list_item(shape, "Widget.ArtShape.Rounded", (int64_t)ArtShape::Rounded);
	add_list_item(shape, "Widget.ArtShape.Circle", (int64_t)ArtShape::Circle);

	obs_property_t *override_radius =
		obs_properties_add_bool(group, "radius_override", obs_module_text("Widget.RadiusOverride"));
	obs_property_set_modified_callback(override_radius, refresh_properties);

	obs_property_t *radius =
		obs_properties_add_float_slider(group, "radius", obs_module_text("Bar.CornerRadius"), 0.0, 256.0, 1.0);
	obs_property_float_set_suffix(radius, " px");

	obs_property_t *animation = obs_properties_add_list(group, "art_animation",
							    obs_module_text("Widget.Art.Animation"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(animation, obs_module_text("Widget.Art.Animation.None"), (int64_t)ArtAnimation::None);
	obs_property_list_add_int(animation, obs_module_text("Widget.Art.Animation.Spin"), (int64_t)ArtAnimation::Spin);
	obs_property_list_add_int(animation, obs_module_text("Widget.Art.Animation.Pulse"),
				  (int64_t)ArtAnimation::Pulse);
	obs_property_set_modified_callback(animation, refresh_properties);
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

	obs_property_t *time_format = add_list(group, "time_format", "Info.TimeFormat");
	add_list_item(time_format, "Info.TimeFormat.Auto", (int64_t)TimeFormat::Auto);
	add_list_item(time_format, "Info.TimeFormat.MSS", (int64_t)TimeFormat::MSS);
	add_list_item(time_format, "Info.TimeFormat.MMSS", (int64_t)TimeFormat::MMSS);
	add_list_item(time_format, "Info.TimeFormat.HMMSS", (int64_t)TimeFormat::HMMSS);
	add_list_item(time_format, "Info.TimeFormat.HHMMSS", (int64_t)TimeFormat::HHMMSS);

	obs_property_t *transition_ms = obs_properties_add_int_slider(
		group, "transition_ms", obs_module_text("Widget.TransitionDuration"), 0, 3000, 10);
	obs_property_int_set_suffix(transition_ms, " ms");

	obs_property_t *idle_trigger = add_list(group, "idle_trigger", "Common.IdleTrigger");
	add_list_item(idle_trigger, "Common.IdleTrigger.Stopped", (int64_t)WidgetIdleTrigger::Stopped);
	add_list_item(idle_trigger, "Common.IdleTrigger.StoppedOrPaused", (int64_t)WidgetIdleTrigger::StoppedOrPaused);
	obs_property_set_long_description(idle_trigger, obs_module_text("Common.IdleTrigger.Description"));

	obs_properties_add_group(props, "tuning_group", obs_module_text("Widget.Group.Tuning"), OBS_GROUP_NORMAL,
				 group);
}

void add_bar_groups(obs_properties_t *props)
{
	obs_properties_t *position = obs_properties_create();
	obs_properties_add_bool(position, "bar_enable", obs_module_text("Widget.Visible"));
	add_geometry_properties(position, "bar_", false);
	obs_properties_add_group(props, "bar_details", obs_module_text("Widget.BarPositionGroup"), OBS_GROUP_NORMAL,
				 position);

	obs_properties_t *style = obs_properties_create();
	BarRenderer::add_properties(style, "bar_", false, true);
	obs_properties_add_group(props, "bar_style", obs_module_text("Widget.BarStyle"), OBS_GROUP_NORMAL, style);
}

} // namespace

obs_properties_t *widget_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	MusicLink::add_property(props);

	add_layout_group(props, data);
	add_style_group(props);

	for (int slot = 0; slot < MAIN_LINES; slot++)
		add_line_main(props, slot);

	add_text_group(props);

	obs_property_t *advanced =
		obs_properties_add_bool(props, "show_advanced", obs_module_text("Widget.ShowAdvanced"));
	obs_property_set_modified_callback(advanced, refresh_properties);

	/* Everything below is advanced. */
	add_tuning_group(props);

	for (int slot = MAIN_LINES; slot < TEXT_SLOTS; slot++)
		add_line_main(props, slot);

	for (int slot = 0; slot < TEXT_SLOTS; slot++)
		add_line_details(props, slot);

	add_art_properties(props);
	add_background_properties(props);
	add_bar_groups(props);

	return props;
}

} // namespace vr::widget
