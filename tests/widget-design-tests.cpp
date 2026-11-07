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

	return test::finish();
}
