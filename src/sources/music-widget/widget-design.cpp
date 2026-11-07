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
	obs_data_set_bool(out, "bg_enable", t.panel);
	obs_data_set_int(out, "bg_type", (int64_t)t.panel_type);
	obs_data_set_int(out, "bg_color", t.panel_color);
	obs_data_set_bool(out, "bg_accent", false);
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

	obs_data_set_int(out, key_for(prefix, "color").c_str(), color);
	obs_data_set_bool(out, key_for(prefix, "accent").c_str(), false);
	obs_data_set_bool(out, key_for(prefix, "gradient").c_str(), false);
	obs_data_set_double(out, key_for(prefix, "outline").c_str(), 0.0);

	obs_data_set_bool(out, key_for(prefix, "shadow").c_str(), t.text_shadow);
	obs_data_set_double(out, key_for(prefix, "shadow_x").c_str(), 0.0);
	obs_data_set_double(out, key_for(prefix, "shadow_y").c_str(), 2.0);
	obs_data_set_double(out, key_for(prefix, "shadow_blur").c_str(), 8.0);
	obs_data_set_int(out, key_for(prefix, "shadow_color").c_str(), 0xC0000000);

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

	if (styled) {
		apply_panel(out, tokens, radius);
		apply_art(out, tokens, shape, radius);
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
		if (styled)
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

		if (styled)
			obs_data_set_double(out, "bar_corner_radius",
					    (double)obs_data_get_int(out, "bar_height") * 0.5);
	}

	obs_data_release(font);
	return out;
}

/* ------------------------------------------------------------------------- */
/* Editing                                                                    */
/* ------------------------------------------------------------------------- */

std::vector<ElementBox> element_boxes(obs_data_t *settings)
{
	obs_data_t *resolved = resolve_settings(settings);

	const uint32_t width = (uint32_t)std::max<int64_t>(1, obs_data_get_int(resolved, "width"));
	const uint32_t height = (uint32_t)std::max<int64_t>(1, obs_data_get_int(resolved, "height"));

	std::vector<ElementBox> boxes;

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
		boxes.push_back(element);
	};

	add("art_", "Widget.Art");
	for (int slot = 0; slot < TEXT_SLOTS; slot++)
		add(text_prefix(slot), line_label_key(slot));
	add("bar_", "Widget.Bar");

	obs_data_release(resolved);
	return boxes;
}

void detach_to_free_placement(obs_data_t *settings)
{
	const Arrangement layout = (Arrangement)obs_data_get_int(settings, "layout");
	if (layout == Arrangement::Free)
		return;

	for (const ElementBox &element : element_boxes(settings))
		set_geometry(settings, element.prefix, element.enabled, element.box);

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

} // namespace vr::widget
