/*
 * Tests for the Music Widget's automatic layouts: where the art, lines and bar
 * land for each arrangement, and that nothing ever spills out of the card or
 * overlaps, at any size.
 */

#include "sources/music-widget/widget-layout.hpp"
#include "test-check.hpp"

#include <string>

using namespace vr::widget;
using test::check;
using test::check_near;
using test::check_true;

namespace {

LayoutRequest request(Arrangement arrangement, float width, float height, int lines = 2)
{
	LayoutRequest r;
	r.arrangement = arrangement;
	r.width = width;
	r.height = height;
	const LineRole roles[] = {LineRole::Title, LineRole::Subtitle, LineRole::Detail,
				  LineRole::Time,  LineRole::Detail,   LineRole::Detail};
	for (int i = 0; i < lines; i++)
		r.lines.push_back({roles[i], 0.0f});
	return r;
}

bool inside(const Box &box, float width, float height)
{
	const float slack = 0.5f;
	return !box.visible || (box.x >= -slack && box.y >= -slack && box.x + box.width <= width + slack &&
				box.y + box.height <= height + slack);
}

bool overlaps(const Box &a, const Box &b)
{
	if (!a.visible || !b.visible)
		return false;
	const float slack = 0.5f;
	return a.x + slack < b.x + b.width && b.x + slack < a.x + a.width && a.y + slack < b.y + b.height &&
	       b.y + slack < a.y + a.height;
}

/* Everything inside the card, and nothing on top of anything else. */
bool tidy(const LayoutResult &result, float width, float height)
{
	std::vector<Box> boxes;
	if (!result.art_as_backdrop)
		boxes.push_back(result.art);
	boxes.push_back(result.bar);
	for (const LayoutLineResult &line : result.lines)
		boxes.push_back(line.box);

	for (size_t i = 0; i < boxes.size(); i++) {
		if (!inside(boxes[i], width, height))
			return false;
		for (size_t j = i + 1; j < boxes.size(); j++)
			if (overlaps(boxes[i], boxes[j]))
				return false;
	}
	return true;
}

} // namespace

int main()
{
	/* Art on the left: a square, then the text beside it, then the bar. */
	{
		const LayoutResult r = compute_layout(request(Arrangement::ArtLeft, 640, 180));
		check_true("art left: art visible", r.art.visible);
		check_near("art left: art is square", r.art.width, r.art.height, 0.01);
		check_true("art left: text right of art", r.lines[0].box.x >= r.art.x + r.art.width);
		check_true("art left: title above subtitle", r.lines[0].box.y < r.lines[1].box.y);
		check_true("art left: title bigger", r.lines[0].font_px > r.lines[1].font_px);
		check_true("art left: title bold", r.lines[0].bold && !r.lines[1].bold);
		check_true("art left: bar under the text", r.bar.y >= r.lines[1].box.y + r.lines[1].box.height);
		check_true("art left: tidy", tidy(r, 640, 180));
	}

	/* Resizing reflows: the text column takes up the new width. */
	{
		const LayoutResult narrow = compute_layout(request(Arrangement::ArtLeft, 640, 180));
		const LayoutResult wide = compute_layout(request(Arrangement::ArtLeft, 960, 180));
		check_near("reflow: art keeps its size", wide.art.width, narrow.art.width, 0.01);
		check_near("reflow: text column grows", wide.lines[0].box.width - narrow.lines[0].box.width, 320, 0.5);
		check_near("reflow: bar grows with it", wide.bar.width - narrow.bar.width, 320, 0.5);

		const LayoutResult tall = compute_layout(request(Arrangement::ArtLeft, 640, 270));
		check_true("reflow: taller widget, bigger type", tall.lines[0].font_px > narrow.lines[0].font_px);
	}

	/* Art on the right mirrors it. */
	{
		const LayoutResult r = compute_layout(request(Arrangement::ArtRight, 640, 180));
		check_true("art right: art after the text", r.art.x >= r.lines[0].box.x + r.lines[0].box.width);
		check_true("art right: tidy", tidy(r, 640, 180));
	}

	/* Stacked: art centred on top, centred text below. */
	{
		const LayoutResult r = compute_layout(request(Arrangement::Stacked, 360, 480, 3));
		check_near("stacked: art centred", r.art.x + r.art.width / 2, 180, 0.5);
		check_true("stacked: text below art", r.lines[0].box.y >= r.art.y + r.art.height);
		check_true("stacked: text centred", r.center_text);
		check_true("stacked: tidy", tidy(r, 360, 480));
	}

	/* Backdrop: art fills the card behind text sitting at the bottom. */
	{
		const LayoutResult r = compute_layout(request(Arrangement::Backdrop, 560, 300));
		check_true("backdrop: art is the backdrop", r.art_as_backdrop && !r.art.visible);
		check_true("backdrop: text in the lower half", r.lines[0].box.y > 150);
		check_true("backdrop: tidy", tidy(r, 560, 300));
	}

	/* Text only has no art. */
	{
		const LayoutResult r = compute_layout(request(Arrangement::TextOnly, 560, 120));
		check_true("text only: no art", !r.art.visible);
		check_true("text only: tidy", tidy(r, 560, 120));
	}

	/* Bar placement. */
	{
		LayoutRequest q = request(Arrangement::Compact, 420, 64);
		q.bar = BarPlacement::BottomEdge;
		q.corner_radius = 20;
		const LayoutResult edge = compute_layout(q);
		check_near("bar on the edge: at the bottom", edge.bar.y + edge.bar.height, 64, 0.01);
		check_near("bar on the edge: clear of the corners", edge.bar.x, 14, 0.01);

		q.bar = BarPlacement::Off;
		check_true("bar off: not shown", !compute_layout(q).bar.visible);
	}

	/* A line that keeps its own font keeps its size. */
	{
		LayoutRequest q = request(Arrangement::ArtLeft, 640, 180);
		q.lines[1].fixed_px = 17.0f;
		check_near("fixed size is kept", compute_layout(q).lines[1].font_px, 17, 0.001);
	}

	/* Text size scales the type. */
	{
		LayoutRequest q = request(Arrangement::ArtLeft, 640, 180);
		const float normal = compute_layout(q).lines[0].font_px;
		q.text_scale = 1.2f;
		check_near("text size 120%", compute_layout(q).lines[0].font_px / normal, 1.2, 0.001);
	}

	/* Too many lines for the space: they shrink to fit rather than spill. */
	{
		const LayoutResult r = compute_layout(request(Arrangement::ArtLeft, 640, 90, 6));
		check_true("crowded: still tidy", tidy(r, 640, 90));
	}

	/* Every arrangement, at sizes from a thin bar to a tall card, with any
	 * number of lines and either bar placement, stays inside the card and
	 * never overlaps itself. */
	{
		int failures = 0;
		int cases = 0;
		const Arrangement arrangements[] = {Arrangement::ArtLeft, Arrangement::ArtRight,
						    Arrangement::Stacked, Arrangement::Backdrop,
						    Arrangement::Compact, Arrangement::TextOnly};
		for (Arrangement a : arrangements)
			for (float w : {120.0f, 280.0f, 640.0f, 1920.0f})
				for (float h : {32.0f, 64.0f, 180.0f, 480.0f, 1080.0f})
					for (int lines = 0; lines <= 6; lines++)
						for (BarPlacement bar : {BarPlacement::UnderText,
									 BarPlacement::BottomEdge, BarPlacement::Off}) {
							LayoutRequest q = request(a, w, h, lines);
							q.bar = bar;
							q.corner_radius = 16;
							cases++;
							if (!tidy(compute_layout(q), w, h)) {
								if (failures < 5)
									printf("  untidy: arrangement %d %gx%g lines %d bar %d\n",
									       (int)a, w, h, lines, (int)bar);
								failures++;
							}
						}
		check("sweep: untidy layouts", failures, 0);
		check_true("sweep: cases run", cases > 2000);
	}

	return test::finish();
}
