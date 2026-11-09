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

#include "sources/music-widget/widget-design.hpp"
#include "sources/music-widget/widget.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vr::widget {

namespace {

/* Colours are 0xAABBGGRR, as OBS stores them. */
constexpr uint32_t white(uint32_t alpha)
{
	return (alpha << 24) | 0x00FFFFFFu;
}

/* The art's corner radius is clamped to half its size when drawn, so this is
 * simply "round". */
constexpr double CIRCLE_RADIUS = 4096.0;

/* What a named style decides. Everything a style does not mention is left as
 * the user set it. */
struct StyleTokens {
	bool panel = true;
	BackgroundType panel_type = BackgroundType::Color;
	uint32_t panel_color = 0xF0191414;
	bool panel_accent = false;
	AccentTone panel_tone = AccentTone::Dark;
	double blur = 70.0;
	double darken = 45.0;
	double desaturate = 20.0;

	double radius = 14.0;
	double border = 0.0;
	uint32_t border_color = white(0x2E);
	bool border_accent = false;

	ArtShape art = ArtShape::Rounded;
	bool art_shadow = true;
	bool spin = false;

	uint32_t primary = white(0xFF);
	uint32_t secondary = white(0xC2);
	uint32_t muted = white(0x94);
	bool text_shadow = false;

	/* A coloured halo instead of a drop shadow: the shadow follows the
	 * accent, sits under the text and spreads wide. */
	bool text_glow = false;

	/* The subtitle takes a tone of the accent instead of a grey. */
	bool subtitle_accent = false;
	AccentTone subtitle_tone = AccentTone::Light;

	uint32_t bar_track = white(0x38);
};

StyleTokens tokens_for(WidgetStyle style)
{
	StyleTokens t;

	switch (style) {
	case WidgetStyle::Glass:
		t.panel_type = BackgroundType::BlurredArt;
		t.radius = 18.0;
		t.border = 1.0;
		break;

	case WidgetStyle::Minimal:
		t.panel = false;
		t.radius = 0.0;
		t.art = ArtShape::Square;
		t.secondary = white(0xDC);
		t.muted = white(0xB4);
		t.text_shadow = true;
		t.bar_track = white(0x40);
		break;

	case WidgetStyle::Outline:
		/* The accent goes on the border and the bar. A dark cover gives
		 * a dark accent, which would make an accent-coloured title hard
		 * to read on the dark panel. */
		t.panel_color = 0xB8100A0A;
		t.radius = 16.0;
		t.border = 2.0;
		t.border_accent = true;
		break;

	case WidgetStyle::Vinyl:
		t.panel_color = 0xF0161212;
		t.radius = 24.0;
		t.art = ArtShape::Circle;
		t.spin = true;
		break;

	case WidgetStyle::Neon:
		t.panel_color = 0xEC0E0808;
		t.radius = 14.0;
		t.border = 2.0;
		t.border_accent = true;
		t.art_shadow = false;
		t.text_glow = true;
		t.subtitle_accent = true;
		t.subtitle_tone = AccentTone::Light;
		t.bar_track = white(0x24);
		break;

	case WidgetStyle::Cassette:
		/* Warm brown plastic with a cream label. */
		t.panel_color = 0xF2223040;
		t.radius = 10.0;
		t.border = 2.0;
		t.border_color = 0x58C8E6F0;
		t.primary = 0xFFD8EEF6;
		t.secondary = 0xD8B4CCDA;
		t.muted = 0xA8A0B4C0;
		t.bar_track = 0x40C8E6F0;
		break;

	case WidgetStyle::Ticker:
		/* A strip in the dark tone of the album colour, square cornered,
		 * meant for a full width bar along the bottom of the screen. */
		t.panel_color = 0xF0101010;
		t.panel_accent = true;
		t.panel_tone = AccentTone::Dark;
		t.radius = 0.0;
		t.art = ArtShape::Square;
		t.art_shadow = false;
		t.secondary = white(0xD0);
		break;

	case WidgetStyle::Solid:
	default:
		break;
	}

	return t;
}

void set_geometry(obs_data_t *data, const std::string &prefix, bool enabled, const Box &box)
{
	obs_data_set_bool(data, key_for(prefix, "enable").c_str(), enabled);
	obs_data_set_int(data, key_for(prefix, "anchor").c_str(), (int64_t)Anchor::TopLeft);
	obs_data_set_double(data, key_for(prefix, "x").c_str(), std::round(box.x));
	obs_data_set_double(data, key_for(prefix, "y").c_str(), std::round(box.y));
	obs_data_set_int(data, key_for(prefix, "width").c_str(), std::max<int64_t>(1, std::lround(box.width)));
	obs_data_set_int(data, key_for(prefix, "height").c_str(), std::max<int64_t>(1, std::lround(box.height)));
}

Geometry read_geometry_of(obs_data_t *data, const std::string &prefix)
{
	Geometry g;
	g.enabled = obs_data_get_bool(data, key_for(prefix, "enable").c_str());
	g.anchor = (Anchor)obs_data_get_int(data, key_for(prefix, "anchor").c_str());
	g.x = (float)obs_data_get_double(data, key_for(prefix, "x").c_str());
	g.y = (float)obs_data_get_double(data, key_for(prefix, "y").c_str());
	g.width = (float)std::max<int64_t>(1, obs_data_get_int(data, key_for(prefix, "width").c_str()));
	g.height = (float)std::max<int64_t>(1, obs_data_get_int(data, key_for(prefix, "height").c_str()));
	g.z = (int)obs_data_get_int(data, key_for(prefix, "z").c_str());
	return g;
}

/* Writes a fresh font object. The one already there may be shared with the
 * user's own settings, so it is never edited in place. */
void set_font(obs_data_t *data, const std::string &key, obs_data_t *base, double size, bool bold)
{
	obs_data_t *font = obs_data_create();

	const char *face = base ? obs_data_get_string(base, "face") : nullptr;
	const int64_t flags = base ? obs_data_get_int(base, "flags") : 0;

	obs_data_set_string(font, "face", face && *face ? face : "Arial");
	/* No style name, so the bold and italic flags decide the weight. */
	obs_data_set_string(font, "style", "");
	obs_data_set_int(font, "size", std::max<int64_t>(1, std::lround(size)));
	obs_data_set_int(font, "flags", (flags & OBS_FONT_ITALIC) | (bold ? OBS_FONT_BOLD : 0));

	obs_data_set_obj(data, key.c_str(), font);
	obs_data_release(font);
}

int font_size_of(obs_data_t *data, const std::string &key)
{
	obs_data_t *font = obs_data_get_obj(data, key.c_str());
	const int size = font ? (int)obs_data_get_int(font, "size") : 0;
	obs_data_release(font);
	return std::max(1, size);
}

void apply_panel(obs_data_t *out, const StyleTokens &t, double radius)
{
	/* A style can leave the panel out, but never puts back one that has
	 * been hidden. */
	obs_data_set_bool(out, "bg_enable", t.panel && obs_data_get_bool(out, "bg_enable"));
	obs_data_set_int(out, "bg_type", (int64_t)t.panel_type);
	obs_data_set_int(out, "bg_color", t.panel_color);
	obs_data_set_bool(out, "bg_accent", t.panel_accent);
	obs_data_set_int(out, "bg_tone", (int64_t)t.panel_tone);
	obs_data_set_bool(out, "bg_accent2", false);
	obs_data_set_double(out, "bg_blur", t.blur);
	obs_data_set_double(out, "bg_darken", t.darken);
	obs_data_set_double(out, "bg_desaturate", t.desaturate);
	obs_data_set_double(out, "bg_inset", 0.0);
	obs_data_set_double(out, "bg_corner_radius", radius);
	obs_data_set_double(out, "bg_border_width", t.border);
	obs_data_set_int(out, "bg_border_color", t.border_color);
	obs_data_set_bool(out, "bg_border_accent", t.border_accent);
	obs_data_set_double(out, "bg_opacity", 100.0);
}

void apply_art(obs_data_t *out, const StyleTokens &t, ArtShape shape, double radius)
{
	if (shape == ArtShape::FromStyle)
		shape = t.art;

	double art_radius = 0.0;
	if (shape == ArtShape::Rounded)
		art_radius = std::max(4.0, radius * 0.6);
	else if (shape == ArtShape::Circle)
		art_radius = CIRCLE_RADIUS;

	obs_data_set_int(out, "art_fit", (int64_t)ArtFit::Cover);
	obs_data_set_double(out, "art_corner_radius", art_radius);
	obs_data_set_double(out, "art_border_width", 0.0);
	obs_data_set_bool(out, "art_shadow", t.art_shadow);
	obs_data_set_double(out, "art_shadow_x", 0.0);
	obs_data_set_double(out, "art_shadow_y", t.panel ? 4.0 : 8.0);
	obs_data_set_double(out, "art_shadow_blur", t.panel ? 12.0 : 24.0);
	obs_data_set_int(out, "art_shadow_color", t.panel ? 0x80000000 : 0xA0000000);
	obs_data_set_double(out, "art_rotation", 0.0);

	/* A record spins; any other style keeps whatever animation was chosen. */
	if (t.spin && (ArtAnimation)obs_data_get_int(out, "art_animation") == ArtAnimation::None)
		obs_data_set_int(out, "art_animation", (int64_t)ArtAnimation::Spin);
}

void apply_bar(obs_data_t *out, const StyleTokens &t)
{
	obs_data_set_int(out, "bar_bg_type", (int64_t)FillType::Color);
	obs_data_set_int(out, "bar_bg_color", t.bar_track);
	obs_data_set_bool(out, "bar_bg_accent", false);
	obs_data_set_double(out, "bar_bg_opacity", 100.0);
	obs_data_set_int(out, "bar_fg_type", (int64_t)FillType::Color);
	obs_data_set_int(out, "bar_fg_color", white(0xFF));
	obs_data_set_bool(out, "bar_fg_accent", true);
	obs_data_set_double(out, "bar_fg_opacity", 100.0);
	obs_data_set_double(out, "bar_border_width", 0.0);
}

void apply_line(obs_data_t *out, const std::string &prefix, const StyleTokens &t, LineRole role, obs_data_t *font,
		double size, LongText long_text, bool center)
{
	set_font(out, key_for(prefix, "font"), font, size, role_bold(role));

	uint32_t color = t.primary;
	if (role == LineRole::Subtitle)
		color = t.secondary;
	else if (role == LineRole::Detail || role == LineRole::Time)
		color = t.muted;

	const bool accent = t.subtitle_accent && role == LineRole::Subtitle;

	obs_data_set_int(out, key_for(prefix, "color").c_str(), color);
	obs_data_set_bool(out, key_for(prefix, "accent").c_str(), accent);
	obs_data_set_int(out, key_for(prefix, "tone").c_str(),
			 (int64_t)(accent ? t.subtitle_tone : AccentTone::Accent));
	obs_data_set_bool(out, key_for(prefix, "gradient").c_str(), false);
	obs_data_set_double(out, key_for(prefix, "outline").c_str(), 0.0);

	if (t.text_glow) {
		/* Only the title glows; a halo on every line turns to mush. */
		const bool glow = role == LineRole::Title;
		obs_data_set_bool(out, key_for(prefix, "shadow").c_str(), glow);
		obs_data_set_double(out, key_for(prefix, "shadow_x").c_str(), 0.0);
		obs_data_set_double(out, key_for(prefix, "shadow_y").c_str(), 0.0);
		obs_data_set_double(out, key_for(prefix, "shadow_blur").c_str(), std::max(6.0, size * 0.45));
		obs_data_set_int(out, key_for(prefix, "shadow_color").c_str(), 0xFFFFFFFF);
		obs_data_set_bool(out, key_for(prefix, "shadow_accent").c_str(), true);
	} else {
		obs_data_set_bool(out, key_for(prefix, "shadow").c_str(), t.text_shadow);
		obs_data_set_double(out, key_for(prefix, "shadow_x").c_str(), 0.0);
		obs_data_set_double(out, key_for(prefix, "shadow_y").c_str(), 2.0);
		obs_data_set_double(out, key_for(prefix, "shadow_blur").c_str(), 8.0);
		obs_data_set_int(out, key_for(prefix, "shadow_color").c_str(), 0xC0000000);
		obs_data_set_bool(out, key_for(prefix, "shadow_accent").c_str(), false);
	}

	obs_data_set_double(out, key_for(prefix, "letter_spacing").c_str(), 0.0);
	obs_data_set_double(out, key_for(prefix, "line_height").c_str(), 100.0);
	obs_data_set_double(out, key_for(prefix, "rotation").c_str(), 0.0);
	obs_data_set_int(out, key_for(prefix, "align").c_str(),
			 (int64_t)(center ? TextAlign::Center : TextAlign::Left));
	obs_data_set_int(out, key_for(prefix, "valign").c_str(), (int64_t)TextVAlign::Middle);

	TextOverflow overflow = TextOverflow::Marquee;
	if (long_text == LongText::Shrink)
		overflow = TextOverflow::Shrink;
	else if (long_text == LongText::Truncate)
		overflow = TextOverflow::Ellipsis;
	obs_data_set_int(out, key_for(prefix, "overflow").c_str(), (int64_t)overflow);
}

/* The blurred cover behind everything, used by the Backdrop layout. */
void apply_backdrop(obs_data_t *out)
{
	obs_data_set_bool(out, "bg_enable", true);
	obs_data_set_int(out, "bg_type", (int64_t)BackgroundType::BlurredArt);
	obs_data_set_double(out, "bg_blur", 18.0);
	obs_data_set_double(out, "bg_darken", 50.0);
	obs_data_set_double(out, "bg_desaturate", 0.0);
	obs_data_set_double(out, "bg_inset", 0.0);
	obs_data_set_double(out, "bg_opacity", 100.0);
}

const char *line_label_key(int slot)
{
	static const char *const KEYS[] = {"Widget.Line1", "Widget.Line2", "Widget.Line3",
					   "Widget.Line4", "Widget.Line5", "Widget.Line6"};
	return KEYS[std::clamp(slot, 0, TEXT_SLOTS - 1)];
}

} // namespace

/* ------------------------------------------------------------------------- */
/* Defaults and migration                                                     */
/* ------------------------------------------------------------------------- */

void add_design_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "layout", (int64_t)Arrangement::ArtLeft);
	obs_data_set_default_int(settings, "style", (int64_t)WidgetStyle::Glass);
	obs_data_set_default_int(settings, "color_source", (int64_t)ColorSource::Album);
	obs_data_set_default_int(settings, "fixed_color", 0xFFFF6C7C);

	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_string(font, "style", "Regular");
	obs_data_set_int(font, "size", 32);
	obs_data_set_int(font, "flags", 0);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);

	obs_data_set_default_int(settings, "text_scale", 100);
	obs_data_set_default_int(settings, "spacing", 100);
	obs_data_set_default_int(settings, "art_scale", 100);
	obs_data_set_default_bool(settings, "radius_override", false);
	obs_data_set_default_double(settings, "radius", 16.0);
	obs_data_set_default_int(settings, "art_shape", (int64_t)ArtShape::FromStyle);
	obs_data_set_default_int(settings, "bar_position", (int64_t)BarPlacement::UnderText);
	obs_data_set_default_int(settings, "long_text", (int64_t)LongText::Scroll);
	obs_data_set_default_bool(settings, "show_advanced", false);
	obs_data_set_default_bool(settings, "free_backdrop", false);

	/* Elements that keep their own look under a named style, as a line
	 * with its own style does. */
	obs_data_set_default_bool(settings, "bg_custom", false);
	obs_data_set_default_bool(settings, "art_custom", false);
	obs_data_set_default_bool(settings, "bar_custom", false);

	static const LineRole ROLES[TEXT_SLOTS] = {LineRole::Title, LineRole::Subtitle, LineRole::Detail,
						   LineRole::Time,  LineRole::Detail,   LineRole::Detail};

	for (int slot = 0; slot < TEXT_SLOTS; slot++) {
		const std::string prefix = text_prefix(slot);
		obs_data_set_default_int(settings, key_for(prefix, "role").c_str(), (int64_t)ROLES[slot]);
		obs_data_set_default_bool(settings, key_for(prefix, "custom").c_str(), false);
	}
}

void migrate_settings(obs_data_t *settings)
{
	if (obs_data_has_user_value(settings, "layout"))
		return;

	bool saved_before = false;

	for (obs_data_item_t *item = obs_data_first(settings); item; obs_data_item_next(&item)) {
		if (obs_data_item_has_user_value(item)) {
			saved_before = true;
			obs_data_item_release(&item);
			break;
		}
	}

	if (saved_before) {
		obs_data_set_int(settings, "layout", (int64_t)Arrangement::Free);
		obs_data_set_int(settings, "style", (int64_t)WidgetStyle::Custom);
		return;
	}

	/* Written out so that the next load does not take this widget for one
	 * saved before layouts existed. */
	obs_data_set_int(settings, "layout", obs_data_get_int(settings, "layout"));
	obs_data_set_int(settings, "style", obs_data_get_int(settings, "style"));
}

/* ------------------------------------------------------------------------- */
/* Resolving                                                                  */
/* ------------------------------------------------------------------------- */

obs_data_t *resolve_settings(obs_data_t *settings)
{
	obs_data_t *out = obs_data_get_defaults(settings);
	obs_data_apply(out, settings);

	const Arrangement layout = (Arrangement)obs_data_get_int(out, "layout");
	const WidgetStyle style = (WidgetStyle)obs_data_get_int(out, "style");
	const bool styled = style != WidgetStyle::Custom;
	const StyleTokens tokens = tokens_for(style);

	const float width = (float)std::max<int64_t>(1, obs_data_get_int(out, "width"));
	const float height = (float)std::max<int64_t>(1, obs_data_get_int(out, "height"));

	const double radius = obs_data_get_bool(out, "radius_override")
				      ? obs_data_get_double(out, "radius")
				      : (styled ? tokens.radius : obs_data_get_double(out, "bg_corner_radius"));
	const ArtShape shape = (ArtShape)obs_data_get_int(out, "art_shape");
	const LongText long_text = (LongText)obs_data_get_int(out, "long_text");

	const bool own_panel = obs_data_get_bool(out, "bg_custom");
	const bool own_art = obs_data_get_bool(out, "art_custom");
	const bool own_bar = obs_data_get_bool(out, "bar_custom");

	if (styled) {
		if (!own_panel)
			apply_panel(out, tokens, radius);
		if (!own_art)
			apply_art(out, tokens, shape, radius);
		if (!own_bar)
			apply_bar(out, tokens);
	} else if (obs_data_get_bool(out, "radius_override")) {
		obs_data_set_double(out, "bg_corner_radius", radius);
	}

	/* Under the Custom style the art keeps its own look, and only a shape
	 * chosen in the advanced options is applied. */
	if (!styled && shape == ArtShape::Square)
		obs_data_set_double(out, "art_corner_radius", 0.0);
	else if (!styled && shape == ArtShape::Rounded)
		obs_data_set_double(out, "art_corner_radius", std::max(4.0, radius * 0.6));
	else if (!styled && shape == ArtShape::Circle)
		obs_data_set_double(out, "art_corner_radius", CIRCLE_RADIUS);

	obs_data_t *font = obs_data_get_obj(out, "font");

	/* Which lines are on, and which of them the style dresses. */
	struct Line {
		int slot;
		LineRole role;
		bool styled;
	};
	std::vector<Line> lines;

	for (int slot = 0; slot < TEXT_SLOTS; slot++) {
		const std::string prefix = text_prefix(slot);
		if (!obs_data_get_bool(out, key_for(prefix, "enable").c_str()))
			continue;

		Line line;
		line.slot = slot;
		line.role = (LineRole)obs_data_get_int(out, key_for(prefix, "role").c_str());
		line.styled = styled && !obs_data_get_bool(out, key_for(prefix, "custom").c_str());
		lines.push_back(line);
	}

	if (layout != Arrangement::Free) {
		LayoutRequest request;
		request.arrangement = layout;
		request.width = width;
		request.height = height;
		request.spacing = (float)obs_data_get_int(out, "spacing") / 100.0f;
		request.art_scale = (float)obs_data_get_int(out, "art_scale") / 100.0f;
		request.text_scale = (float)obs_data_get_int(out, "text_scale") / 100.0f;
		request.corner_radius = (float)radius;
		request.bar = (BarPlacement)obs_data_get_int(out, "bar_position");

		for (const Line &line : lines) {
			LayoutLine entry;
			entry.role = line.role;
			if (!line.styled)
				entry.fixed_px = (float)font_size_of(out, key_for(text_prefix(line.slot), "font"));
			request.lines.push_back(entry);
		}

		const LayoutResult result = compute_layout(request);

		const bool show_art = result.art.visible && layout != Arrangement::TextOnly;
		set_geometry(out, "art_", show_art, result.art);

		if (result.art_as_backdrop)
			apply_backdrop(out);

		for (size_t i = 0; i < lines.size(); i++) {
			const Line &line = lines[i];
			const std::string prefix = text_prefix(line.slot);
			const LayoutLineResult &placed = result.lines[i];

			set_geometry(out, prefix, placed.box.visible, placed.box);

			if (line.styled)
				apply_line(out, prefix, tokens, line.role, font, placed.font_px, long_text,
					   result.center_text);
		}

		set_geometry(out, "bar_", result.bar.visible, result.bar);
		if (styled && !own_bar)
			obs_data_set_double(out, "bar_corner_radius", result.bar.height * 0.5);
	} else {
		if (obs_data_get_bool(out, "free_backdrop"))
			apply_backdrop(out);

		/* Placed by hand: a styled line's type follows its box, so making
		 * the box taller in the editor makes the text bigger. */
		for (const Line &line : lines) {
			if (!line.styled)
				continue;

			const std::string prefix = text_prefix(line.slot);
			const double box = (double)obs_data_get_int(out, key_for(prefix, "height").c_str());
			apply_line(out, prefix, tokens, line.role, font, box / LINE_BOX_RATIO, long_text, false);
		}

		if (styled && !own_bar)
			obs_data_set_double(out, "bar_corner_radius",
					    (double)obs_data_get_int(out, "bar_height") * 0.5);
	}

	obs_data_release(font);
	return out;
}

/* ------------------------------------------------------------------------- */
/* Editing                                                                    */
/* ------------------------------------------------------------------------- */

bool is_always_free(const std::string &prefix)
{
	return prefix == "viz_" || prefix == "img_" || prefix == "icon_" || prefix == "glow_";
}

std::string z_key(const std::string &prefix)
{
	return prefix + "z";
}

std::vector<ElementBox> element_boxes(obs_data_t *settings)
{
	obs_data_t *resolved = resolve_settings(settings);

	const uint32_t width = (uint32_t)std::max<int64_t>(1, obs_data_get_int(resolved, "width"));
	const uint32_t height = (uint32_t)std::max<int64_t>(1, obs_data_get_int(resolved, "height"));

	std::vector<ElementBox> boxes;

	/* The panel is listed so it can be styled, locked and restacked, but it
	 * always fills the widget, less its inset. */
	{
		const float inset = std::max(0.0f, (float)obs_data_get_double(resolved, "bg_inset"));
		ElementBox panel;
		panel.prefix = "bg_";
		panel.label = obs_module_text("Widget.Panel");
		panel.enabled = obs_data_get_bool(resolved, "bg_enable");
		panel.z = (int)obs_data_get_int(resolved, "bg_z");
		panel.box.x = inset;
		panel.box.y = inset;
		panel.box.width = std::max(1.0f, (float)width - inset * 2.0f);
		panel.box.height = std::max(1.0f, (float)height - inset * 2.0f);
		panel.box.visible = panel.enabled;
		panel.movable = false;
		panel.locked = obs_data_get_bool(settings, "bg_locked");
		boxes.push_back(panel);
	}

	const auto add = [&](const std::string &prefix, const char *label) {
		const Geometry g = read_geometry_of(resolved, prefix);

		ElementBox element;
		element.prefix = prefix;
		element.label = obs_module_text(label);
		element.enabled = g.enabled;
		element.z = g.z;
		resolve_position(g, width, height, element.box.x, element.box.y);
		element.box.width = g.width;
		element.box.height = g.height;
		element.box.visible = g.enabled;
		element.always_free = is_always_free(prefix);
		element.locked = obs_data_get_bool(settings, key_for(prefix, "locked").c_str());
		boxes.push_back(element);
	};

	add("glow_", "Widget.Glow");
	add("art_", "Widget.Art");
	for (int slot = 0; slot < TEXT_SLOTS; slot++)
		add(text_prefix(slot), line_label_key(slot));
	add("bar_", "Widget.Bar");
	add("viz_", "Widget.Visualizer");
	add("img_", "Widget.Image");
	add("icon_", "Widget.Icon");

	obs_data_release(resolved);
	return boxes;
}

void set_stacking(obs_data_t *settings, const std::vector<std::string> &order)
{
	/* Spaced out so a layer typed in by hand can still go between two. */
	int z = -100;
	for (const std::string &prefix : order) {
		obs_data_set_int(settings, z_key(prefix).c_str(), z);
		z += 10;
	}
}

void detach_to_free_placement(obs_data_t *settings)
{
	const Arrangement layout = (Arrangement)obs_data_get_int(settings, "layout");
	if (layout == Arrangement::Free)
		return;

	for (const ElementBox &element : element_boxes(settings)) {
		if (element.always_free || !element.movable)
			continue;
		set_geometry(settings, element.prefix, element.enabled, element.box);
	}

	obs_data_set_bool(settings, "free_backdrop", layout == Arrangement::Backdrop);
	obs_data_set_int(settings, "layout", (int64_t)Arrangement::Free);
}

void place_element(obs_data_t *settings, const std::string &prefix, const Box &box)
{
	obs_data_t *resolved = resolve_settings(settings);

	const uint32_t width = (uint32_t)std::max<int64_t>(1, obs_data_get_int(resolved, "width"));
	const uint32_t height = (uint32_t)std::max<int64_t>(1, obs_data_get_int(resolved, "height"));

	/* Kept relative to the element's own anchor, so a right-anchored element
	 * still follows the right edge when the widget is resized. */
	Geometry g = read_geometry_of(resolved, prefix);
	g.x = 0.0f;
	g.y = 0.0f;
	g.width = std::max(1.0f, std::round(box.width));
	g.height = std::max(1.0f, std::round(box.height));

	float origin_x = 0.0f;
	float origin_y = 0.0f;
	resolve_position(g, width, height, origin_x, origin_y);

	obs_data_set_double(settings, key_for(prefix, "x").c_str(), std::round(box.x - origin_x));
	obs_data_set_double(settings, key_for(prefix, "y").c_str(), std::round(box.y - origin_y));
	obs_data_set_int(settings, key_for(prefix, "width").c_str(), (int64_t)g.width);
	obs_data_set_int(settings, key_for(prefix, "height").c_str(), (int64_t)g.height);

	obs_data_release(resolved);
}

/* ------------------------------------------------------------------------- */
/* Designs                                                                    */
/* ------------------------------------------------------------------------- */

namespace {

constexpr const char *DESIGN_MARKER = "void_radio_widget_design";

/* What a design never carries: which music source to follow belongs to the
 * widget, not to how it looks. */
bool is_binding_key(const char *name)
{
	return strcmp(name, "music_source") == 0 || strcmp(name, "music_source_uuid") == 0;
}

void set_line(obs_data_t *d, int slot, bool enabled, const char *format, LineRole role)
{
	const std::string prefix = text_prefix(slot);
	obs_data_set_bool(d, key_for(prefix, "enable").c_str(), enabled);
	obs_data_set_string(d, key_for(prefix, "format").c_str(), format);
	obs_data_set_int(d, key_for(prefix, "role").c_str(), (int64_t)role);
}

std::string design_json(obs_data_t *d)
{
	obs_data_t *wrapper = obs_data_create();
	obs_data_set_int(wrapper, DESIGN_MARKER, 1);
	obs_data_set_obj(wrapper, "settings", d);
	const std::string json = obs_data_get_json(wrapper);
	obs_data_release(wrapper);
	obs_data_release(d);
	return json;
}

obs_data_t *base_design(Arrangement layout, WidgetStyle style, int width, int height)
{
	obs_data_t *d = obs_data_create();
	obs_data_set_int(d, "layout", (int64_t)layout);
	obs_data_set_int(d, "style", (int64_t)style);
	obs_data_set_int(d, "width", width);
	obs_data_set_int(d, "height", height);
	return d;
}

} // namespace

std::string export_design(obs_data_t *settings)
{
	obs_data_t *design = obs_data_create();

	for (obs_data_item_t *item = obs_data_first(settings); item; obs_data_item_next(&item)) {
		const char *name = obs_data_item_get_name(item);
		if (!name || is_binding_key(name) || !obs_data_item_has_user_value(item))
			continue;

		switch (obs_data_item_gettype(item)) {
		case OBS_DATA_STRING:
			obs_data_set_string(design, name, obs_data_item_get_string(item));
			break;
		case OBS_DATA_NUMBER:
			if (obs_data_item_numtype(item) == OBS_DATA_NUM_DOUBLE)
				obs_data_set_double(design, name, obs_data_item_get_double(item));
			else
				obs_data_set_int(design, name, obs_data_item_get_int(item));
			break;
		case OBS_DATA_BOOLEAN:
			obs_data_set_bool(design, name, obs_data_item_get_bool(item));
			break;
		case OBS_DATA_OBJECT: {
			obs_data_t *obj = obs_data_item_get_obj(item);
			obs_data_set_obj(design, name, obj);
			obs_data_release(obj);
			break;
		}
		case OBS_DATA_ARRAY: {
			obs_data_array_t *array = obs_data_item_get_array(item);
			obs_data_set_array(design, name, array);
			obs_data_array_release(array);
			break;
		}
		default:
			break;
		}
	}

	/* Always written, so applying the design never leaves a widget on a
	 * layout or style it merely defaulted to. */
	obs_data_set_int(design, "layout", obs_data_get_int(settings, "layout"));
	obs_data_set_int(design, "style", obs_data_get_int(settings, "style"));

	return design_json(design);
}

bool apply_design(obs_data_t *settings, const char *json)
{
	if (!json || !*json)
		return false;

	obs_data_t *parsed = obs_data_create_from_json(json);
	if (!parsed)
		return false;

	obs_data_t *design = nullptr;
	if (obs_data_has_user_value(parsed, DESIGN_MARKER))
		design = obs_data_get_obj(parsed, "settings");
	else {
		design = parsed;
		obs_data_addref(parsed);
	}
	obs_data_release(parsed);

	/* A bare object is only taken for a design if it says what layout it
	 * has, so an unrelated JSON file is refused rather than wiping the
	 * widget. */
	if (!design || !obs_data_has_user_value(design, "layout")) {
		obs_data_release(design);
		return false;
	}

	const std::string source = obs_data_get_string(settings, "music_source");
	const std::string uuid = obs_data_get_string(settings, "music_source_uuid");

	obs_data_clear(settings);

	obs_data_unset_user_value(design, "music_source");
	obs_data_unset_user_value(design, "music_source_uuid");

	obs_data_apply(settings, design);
	obs_data_release(design);

	if (!source.empty())
		obs_data_set_string(settings, "music_source", source.c_str());
	if (!uuid.empty())
		obs_data_set_string(settings, "music_source_uuid", uuid.c_str());

	if (!obs_data_has_user_value(settings, "style"))
		obs_data_set_int(settings, "style", obs_data_get_int(settings, "style"));

	return true;
}

std::vector<BuiltinDesign> builtin_designs()
{
	std::vector<BuiltinDesign> designs;

	{
		obs_data_t *d = base_design(Arrangement::ArtLeft, WidgetStyle::Glass, 720, 150);
		set_line(d, 0, true, "{title}", LineRole::Title);
		set_line(d, 1, true, "{artist}", LineRole::Subtitle);
		set_line(d, 2, true, "{album}[ · {year}]", LineRole::Detail);
		designs.push_back({"Preset.LowerThird", design_json(d)});
	}

	{
		obs_data_t *d = base_design(Arrangement::ArtLeft, WidgetStyle::Solid, 720, 170);
		set_line(d, 0, true, "{title}", LineRole::Title);
		set_line(d, 1, true, "{artist}", LineRole::Subtitle);
		set_line(d, 2, true, "Up next · {next_title}[ — {next_artist}]", LineRole::Detail);
		designs.push_back({"Preset.UpNext", design_json(d)});
	}

	{
		obs_data_t *d = base_design(Arrangement::Stacked, WidgetStyle::Minimal, 360, 470);
		set_line(d, 0, true, "{title}", LineRole::Title);
		set_line(d, 1, true, "{artist}", LineRole::Subtitle);
		obs_data_set_int(d, "bar_position", (int64_t)BarPlacement::Off);
		obs_data_set_bool(d, "viz_enable", true);
		obs_data_set_int(d, "viz_anchor", (int64_t)Anchor::BottomCenter);
		obs_data_set_double(d, "viz_x", 0.0);
		obs_data_set_double(d, "viz_y", -10.0);
		obs_data_set_int(d, "viz_width", 300);
		obs_data_set_int(d, "viz_height", 36);
		obs_data_set_int(d, "viz_mode", (int64_t)VizMode::Mirror);
		designs.push_back({"Preset.BigCover", design_json(d)});
	}

	{
		obs_data_t *d = base_design(Arrangement::Compact, WidgetStyle::Ticker, 1280, 56);
		set_line(d, 0, true, "Now playing · {title}[ — {artist}]", LineRole::Title);
		set_line(d, 1, false, "", LineRole::Subtitle);
		obs_data_set_int(d, "bar_position", (int64_t)BarPlacement::BottomEdge);
		obs_data_set_bool(d, "icon_enable", true);
		obs_data_set_int(d, "icon_anchor", (int64_t)Anchor::MiddleRight);
		obs_data_set_double(d, "icon_x", -16.0);
		obs_data_set_double(d, "icon_y", 0.0);
		obs_data_set_int(d, "icon_width", 22);
		obs_data_set_int(d, "icon_height", 22);
		designs.push_back({"Preset.Ticker", design_json(d)});
	}

	{
		obs_data_t *d = base_design(Arrangement::ArtLeft, WidgetStyle::Neon, 680, 170);
		set_line(d, 0, true, "{title}", LineRole::Title);
		set_line(d, 1, true, "{artist}", LineRole::Subtitle);
		obs_data_set_bool(d, "viz_enable", true);
		obs_data_set_int(d, "viz_anchor", (int64_t)Anchor::TopRight);
		obs_data_set_double(d, "viz_x", -18.0);
		obs_data_set_double(d, "viz_y", 18.0);
		obs_data_set_int(d, "viz_width", 110);
		obs_data_set_int(d, "viz_height", 34);
		obs_data_set_int(d, "viz_bands", 16);
		designs.push_back({"Preset.Neon", design_json(d)});
	}

	{
		obs_data_t *d = base_design(Arrangement::ArtLeft, WidgetStyle::Cassette, 640, 160);
		set_line(d, 0, true, "{title}", LineRole::Title);
		set_line(d, 1, true, "{artist}", LineRole::Subtitle);
		set_line(d, 3, true, "{elapsed} / {duration}", LineRole::Time);
		designs.push_back({"Preset.Cassette", design_json(d)});
	}

	{
		obs_data_t *d = base_design(Arrangement::ArtLeft, WidgetStyle::Glass, 560, 130);
		set_line(d, 0, true, "{title}", LineRole::Title);
		set_line(d, 1, true, "{artist}", LineRole::Subtitle);
		obs_data_set_int(d, "popup_mode", (int64_t)PopupMode::OnTrackChange);
		obs_data_set_int(d, "popup_animation", (int64_t)PopupAnimation::SlideUp);
		obs_data_set_int(d, "enter_style", (int64_t)EnterStyle::SlideUp);
		designs.push_back({"Preset.PopIn", design_json(d)});
	}

	return designs;
}

} // namespace vr::widget
