/*
 * Tests for how the Music Widget's layout, style and line settings turn into
 * per-element settings, and for the editor's promises: switching to free
 * placement moves nothing, and a placed element lands where it was put.
 *
 * Needs libobs for its settings objects, so it is built alongside the smoke
 * test when libobs is installed.
 */

#include "sources/music-widget/widget-design.hpp"
#include "sources/music-widget/widget.hpp"
#include "test-check.hpp"

#include <obs-module.h>

#include <cmath>
#include <string>
#include <vector>

using namespace vr::widget;
using test::check;
using test::check_near;
using test::check_true;

/* What the plugin's module glue would provide. */
extern "C" const char *obs_module_text(const char *lookup)
{
	return lookup;
}

extern "C" obs_module_t *obs_current_module(void)
{
	return nullptr;
}

extern "C" void obs_log(int, const char *, ...) {}

namespace {

obs_data_t *new_widget(const char *json = "{}")
{
	obs_data_t *settings = obs_data_create_from_json(json);
	widget_defaults(settings);
	return settings;
}

int font_size(obs_data_t *data, const char *key)
{
	obs_data_t *font = obs_data_get_obj(data, key);
	const int size = (int)obs_data_get_int(font, "size");
	obs_data_release(font);
	return size;
}

const ElementBox *find(const std::vector<ElementBox> &boxes, const std::string &prefix)
{
	for (const ElementBox &box : boxes)
		if (box.prefix == prefix)
			return &box;
	return nullptr;
}

bool same_box(const Box &a, const Box &b)
{
	return std::fabs(a.x - b.x) <= 0.5f && std::fabs(a.y - b.y) <= 0.5f && std::fabs(a.width - b.width) <= 0.5f &&
	       std::fabs(a.height - b.height) <= 0.5f;
}

} // namespace

int main()
{
	/* A new widget: art on the left, Glass, title and artist. */
	{
		obs_data_t *settings = new_widget();
		obs_data_t *r = resolve_settings(settings);

		check("new: art placed by the layout", obs_data_get_int(r, "art_anchor"), (long long)Anchor::TopLeft);
		check_true("new: art shown", obs_data_get_bool(r, "art_enable"));
		check_true("new: title shown", obs_data_get_bool(r, "t0_enable"));
		check_true("new: detail line off", !obs_data_get_bool(r, "t2_enable"));
		check("new: glass panel is blurred art", obs_data_get_int(r, "bg_type"),
		      (long long)BackgroundType::BlurredArt);
		check_true("new: title larger than artist", font_size(r, "t0_font") > font_size(r, "t1_font"));
		check_true("new: bar follows the album colour", obs_data_get_bool(r, "bar_fg_accent"));

		/* The user's own settings are never written to. */
		check_true("new: settings untouched", !obs_data_has_user_value(settings, "art_x"));

		obs_data_release(r);
		obs_data_release(settings);
	}

	/* Resizing reflows: the title gets wider with the widget. */
	{
		obs_data_t *settings = new_widget("{\"width\": 640}");
		obs_data_t *a = resolve_settings(settings);
		obs_data_set_int(settings, "width", 960);
		obs_data_t *b = resolve_settings(settings);
		check("resize: title grows with the widget",
		      obs_data_get_int(b, "t0_width") - obs_data_get_int(a, "t0_width"), 320);
		obs_data_release(a);
		obs_data_release(b);
		obs_data_release(settings);
	}

	/* A line that styles itself keeps its own font and colour. */
	{
		obs_data_t *settings = new_widget("{\"t1_custom\": true, \"t1_color\": 4278190335}");
		obs_data_t *font = obs_data_create();
		obs_data_set_string(font, "face", "Georgia");
		obs_data_set_int(font, "size", 19);
		obs_data_set_obj(settings, "t1_font", font);
		obs_data_release(font);

		obs_data_t *r = resolve_settings(settings);
		check("own style: font size kept", font_size(r, "t1_font"), 19);
		check("own style: colour kept", obs_data_get_int(r, "t1_color"), 4278190335LL);
		check_true("own style: still placed by the layout", obs_data_get_int(r, "t1_x") > 0);
		obs_data_release(r);
		obs_data_release(settings);
	}

	/* Custom style: every element keeps the look it was given by hand. */
	{
		obs_data_t *settings = new_widget("{\"style\": 0, \"bg_type\": 1, \"bg_color\": 4278255360}");
		obs_data_t *r = resolve_settings(settings);
		check("custom: background type kept", obs_data_get_int(r, "bg_type"), 1);
		check("custom: background colour kept", obs_data_get_int(r, "bg_color"), 4278255360LL);
		obs_data_release(r);
		obs_data_release(settings);
	}

	/* Switching to free placement moves nothing, for every layout. */
	for (int layout = 1; layout <= 6; layout++) {
		obs_data_t *settings = new_widget();
		obs_data_set_int(settings, "layout", layout);
		obs_data_set_bool(settings, "t2_enable", true);

		const std::vector<ElementBox> before = element_boxes(settings);
		obs_data_t *r_before = resolve_settings(settings);

		detach_to_free_placement(settings);

		const std::vector<ElementBox> after = element_boxes(settings);
		obs_data_t *r_after = resolve_settings(settings);

		bool unmoved = before.size() == after.size();
		for (size_t i = 0; unmoved && i < before.size(); i++)
			unmoved = before[i].enabled == after[i].enabled &&
				  (!before[i].enabled || same_box(before[i].box, after[i].box));

		const std::string label = "detach layout " + std::to_string(layout);
		check_true((label + ": now free placement").c_str(),
			   obs_data_get_int(settings, "layout") == (int64_t)Arrangement::Free);
		check_true((label + ": nothing moved").c_str(), unmoved);

		bool same_type = true;
		for (const char *key : {"t0_font", "t1_font", "t2_font"})
			same_type = same_type && std::abs(font_size(r_before, key) - font_size(r_after, key)) <= 1;
		check_true((label + ": type the same size").c_str(), same_type);

		check((label + ": backdrop kept").c_str(), obs_data_get_int(r_after, "bg_type"),
		      obs_data_get_int(r_before, "bg_type"));

		obs_data_release(r_before);
		obs_data_release(r_after);
		obs_data_release(settings);
	}

	/* Placing an element puts it exactly where asked, whatever its anchor. */
	for (int anchor = 0; anchor <= 8; anchor++) {
		obs_data_t *settings = new_widget("{\"layout\": 0}");
		obs_data_set_int(settings, "art_anchor", anchor);

		Box want;
		want.x = 37;
		want.y = 21;
		want.width = 90;
		want.height = 80;
		place_element(settings, "art_", want);

		const ElementBox *art = find(element_boxes(settings), "art_");
		check_true(("place with anchor " + std::to_string(anchor)).c_str(), art && same_box(art->box, want));
		check(("anchor " + std::to_string(anchor) + " kept").c_str(), obs_data_get_int(settings, "art_anchor"),
		      anchor);
		obs_data_release(settings);
	}

	/* A styled line placed by hand takes its type size from its box. */
	{
		obs_data_t *settings = new_widget("{\"layout\": 0}");
		Box tall;
		tall.x = 10;
		tall.y = 10;
		tall.width = 300;
		tall.height = 66;
		place_element(settings, "t0_", tall);
		obs_data_t *r = resolve_settings(settings);
		check("free: type follows the box", font_size(r, "t0_font"), 50);
		obs_data_release(r);
		obs_data_release(settings);
	}

	/* Migration: old widgets keep what they had, new ones get the defaults. */
	{
		obs_data_t *old = obs_data_create_from_json("{\"t0_x\": 321.0}");
		widget_defaults(old);
		migrate_settings(old);
		check("old widget: free placement", obs_data_get_int(old, "layout"), (long long)Arrangement::Free);
		check("old widget: custom style", obs_data_get_int(old, "style"), (long long)WidgetStyle::Custom);
		obs_data_t *r = resolve_settings(old);
		check_near("old widget: its own position", obs_data_get_double(r, "t0_x"), 321, 0.001);
		obs_data_release(r);
		obs_data_release(old);

		obs_data_t *fresh = obs_data_create();
		widget_defaults(fresh);
		migrate_settings(fresh);
		check("new widget: art on the left", obs_data_get_int(fresh, "layout"),
		      (long long)Arrangement::ArtLeft);
		check_true("new widget: layout saved, so it is not migrated later",
			   obs_data_has_user_value(fresh, "layout"));
		migrate_settings(fresh);
		check("new widget: stays as it is", obs_data_get_int(fresh, "layout"), (long long)Arrangement::ArtLeft);
		obs_data_release(fresh);
	}

	/* The new styles: Neon glows in the accent, Ticker's panel takes the
	 * album colour's dark tone, Cassette sets its own cream text. */
	{
		obs_data_t *settings = new_widget("{\"style\": 6}");
		obs_data_t *r = resolve_settings(settings);
		check_true("neon: title glows", obs_data_get_bool(r, "t0_shadow"));
		check_true("neon: glow follows the accent", obs_data_get_bool(r, "t0_shadow_accent"));
		check_true("neon: border follows the accent", obs_data_get_bool(r, "bg_border_accent"));
		check_true("neon: subtitle in a tone of the accent", obs_data_get_bool(r, "t1_accent"));
		check("neon: subtitle in the light tone", obs_data_get_int(r, "t1_tone"), (long long)AccentTone::Light);
		obs_data_release(r);
		obs_data_release(settings);

		settings = new_widget("{\"style\": 8}");
		r = resolve_settings(settings);
		check_true("ticker: panel follows the accent", obs_data_get_bool(r, "bg_accent"));
		check("ticker: in the dark tone", obs_data_get_int(r, "bg_tone"), (long long)AccentTone::Dark);
		check_near("ticker: square corners", obs_data_get_double(r, "bg_corner_radius"), 0.0, 0.001);
		obs_data_release(r);
		obs_data_release(settings);

		settings = new_widget("{\"style\": 7}");
		r = resolve_settings(settings);
		check("cassette: cream title", obs_data_get_int(r, "t0_color"), 0xFFD8EEF6LL);
		check_true("cassette: glow off", !obs_data_get_bool(r, "t0_shadow_accent"));
		obs_data_release(r);
		obs_data_release(settings);
	}

	/* An element keeping its own look is left alone by the style. */
	{
		obs_data_t *settings = new_widget("{\"style\": 2, \"bg_custom\": true, \"bg_color\": 4278255360,"
						  " \"bar_custom\": true, \"bar_fg_color\": 4278190335}");
		obs_data_t *r = resolve_settings(settings);
		check("own look: panel colour kept", obs_data_get_int(r, "bg_color"), 4278255360LL);
		check("own look: bar colour kept", obs_data_get_int(r, "bar_fg_color"), 4278190335LL);
		check_true("own look: art still styled", obs_data_get_double(r, "art_corner_radius") > 0.0);
		obs_data_release(r);
		obs_data_release(settings);
	}

	/* The extras are placed by hand under any layout, and moving one does
	 * not switch the widget to free placement. */
	{
		obs_data_t *settings = new_widget("{\"layout\": 1, \"viz_enable\": true}");
		migrate_settings(settings);
		std::vector<ElementBox> boxes = element_boxes(settings);
		const ElementBox *viz = find(boxes, "viz_");
		check_true("extras: visualiser listed", viz != nullptr);
		check_true("extras: placed by hand", viz && viz->always_free);
		check_true("extras: enabled", viz && viz->enabled);
		const ElementBox *panel = find(boxes, "bg_");
		check_true("extras: panel listed and fixed", panel && !panel->movable);

		/* Bottom right, 16 px in: 640 - 16 - 160, 180 - 16 - 48. */
		check_near("extras: anchored x", viz ? viz->box.x : -1, 464, 0.5);
		check_near("extras: anchored y", viz ? viz->box.y : -1, 116, 0.5);

		Box moved = viz->box;
		moved.x -= 40;
		place_element(settings, "viz_", moved);
		check("extras: layout untouched", obs_data_get_int(settings, "layout"),
		      (long long)Arrangement::ArtLeft);
		check("extras: anchor kept", obs_data_get_int(settings, "viz_anchor"), (long long)Anchor::BottomRight);
		check_near("extras: offset from its anchor", obs_data_get_double(settings, "viz_x"), -56, 0.5);

		detach_to_free_placement(settings);
		check("extras: anchor survives detaching", obs_data_get_int(settings, "viz_anchor"),
		      (long long)Anchor::BottomRight);
		obs_data_release(settings);
	}

	/* Stacking writes ascending layers in the order given. */
	{
		obs_data_t *settings = new_widget();
		set_stacking(settings, {"bg_", "t0_", "art_"});
		check_true("stacking: text under art",
			   obs_data_get_int(settings, "t0_z") < obs_data_get_int(settings, "art_z"));
		check_true("stacking: panel at the back",
			   obs_data_get_int(settings, "bg_z") < obs_data_get_int(settings, "t0_z"));
		obs_data_release(settings);
	}

	/* Designs: exported without the music source, applied over another
	 * widget keeping that widget's own source. */
	{
		obs_data_t *a =
			new_widget("{\"layout\": 1, \"music_source\": \"Music A\", \"style\": 4, \"width\": 777,"
				   " \"t0_format\": \"{artist}\"}");
		migrate_settings(a);
		const std::string json = export_design(a);
		check_true("design: marked as one", json.find("void_radio_widget_design") != std::string::npos);
		check_true("design: no music source", json.find("Music A") == std::string::npos);

		obs_data_t *b =
			new_widget("{\"layout\": 1, \"music_source\": \"Music B\", \"style\": 1, \"t2_enable\": true}");
		migrate_settings(b);
		check_true("design: applied", apply_design(b, json.c_str()));
		check("design: style copied", obs_data_get_int(b, "style"), 4);
		check("design: size copied", obs_data_get_int(b, "width"), 777);
		check_true("design: lines copied",
			   std::string(obs_data_get_string(b, "t0_format")) == std::string("{artist}"));
		check_true("design: old settings gone", !obs_data_get_bool(b, "t2_enable"));
		check_true("design: own source kept",
			   std::string(obs_data_get_string(b, "music_source")) == std::string("Music B"));

		check_true("design: junk refused", !apply_design(b, "{\"hello\": 1}"));
		check_true("design: not JSON refused", !apply_design(b, "not json"));
		check("design: refused leaves it alone", obs_data_get_int(b, "width"), 777);

		obs_data_release(a);
		obs_data_release(b);
	}

	/* Every built-in design applies and lays out. */
	for (const BuiltinDesign &design : builtin_designs()) {
		obs_data_t *settings = new_widget();
		const std::string label = std::string("builtin ") + design.name;
		check_true((label + ": applies").c_str(), apply_design(settings, design.json.c_str()));
		obs_data_t *r = resolve_settings(settings);
		check_true((label + ": title on").c_str(), obs_data_get_bool(r, "t0_enable"));
		obs_data_release(r);
		obs_data_release(settings);
	}

	return test::finish();
}
