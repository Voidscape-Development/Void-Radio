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

/* The widget's properties dialog. */

#include "sources/music-widget/widget.hpp"

namespace vr::widget {

/* ------------------------------------------------------------------------- */
/* Properties                                                                 */
/* ------------------------------------------------------------------------- */

static void add_anchor_list(obs_properties_t *props, const std::string &prefix)
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
static void add_geometry_properties(obs_properties_t *props, const std::string &prefix, bool include_opacity = true)
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
static bool background_type_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
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
static bool overflow_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
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
static bool art_animation_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
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

static void add_background_properties(obs_properties_t *props)
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

static void add_art_properties(obs_properties_t *props)
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

static void add_text_properties(obs_properties_t *props, int slot)
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

} // namespace vr::widget
