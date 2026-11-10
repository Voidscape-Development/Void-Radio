/*
 * Tests for the layout editor's snapping: what a dragged or resized element
 * snaps to, and that it leaves alone anything out of reach.
 */

#include "test-check.hpp"
#include "ui/editor-snap.hpp"

using namespace vr::editor;
using test::check;
using test::check_near;
using test::check_true;

int main()
{
	const float W = 640, H = 180;

	/* A move near the left edge snaps to it. */
	{
		const SnapResult r = snap({4, 30, 100, 40}, EdgeAll, W, H, {}, 6);
		check_near("move: snaps to the left edge", r.rect.x, 0, 0.001);
		check_near("move: size unchanged", r.rect.width, 100, 0.001);
		check("move: one vertical guide", (long long)r.guides.size(), 1);
		check_true("move: guide at the edge", r.guides[0].vertical && r.guides[0].position == 0);
	}

	/* The centre of the element snaps to the centre of the widget. */
	{
		const SnapResult r = snap({268, 70, 100, 40}, EdgeAll, W, H, {}, 6);
		check_near("move: centred horizontally", r.rect.x + r.rect.width / 2, 320, 0.001);
		check_near("move: centred vertically", r.rect.y + r.rect.height / 2, 90, 0.001);
		check("move: two guides", (long long)r.guides.size(), 2);
	}

	/* Out of reach, nothing moves. */
	{
		const SnapResult r = snap({40, 30, 100, 40}, EdgeAll, W, H, {}, 6);
		check_near("far: x kept", r.rect.x, 40, 0.001);
		check_near("far: y kept", r.rect.y, 30, 0.001);
		check("far: no guides", (long long)r.guides.size(), 0);
	}

	/* Other elements' edges are targets too: line up under the art. */
	{
		const Rect art{20, 20, 140, 140};
		const SnapResult r = snap({183, 23, 300, 40}, EdgeAll, W, H, {art}, 6);
		check_near("align: top with the art's top", r.rect.y, 20, 0.001);
		check_near("align: x untouched", r.rect.x, 183, 0.001);
	}

	/* The closest target wins. */
	{
		const SnapResult r = snap({3, 50, 100, 40}, EdgeAll, W, H, {{5, 0, 10, 10}}, 6);
		check_near("closest: snaps 2px rather than 3px", r.rect.x, 5, 0.001);
	}

	/* Resizing moves only the dragged edge. */
	{
		const SnapResult r = snap({100, 50, 536, 40}, EdgeRight, W, H, {}, 6);
		check_near("resize right: x fixed", r.rect.x, 100, 0.001);
		check_near("resize right: right edge on the widget edge", r.rect.x + r.rect.width, 640, 0.001);

		const SnapResult l = snap({4, 50, 200, 40}, EdgeLeft, W, H, {}, 6);
		check_near("resize left: left edge snapped", l.rect.x, 0, 0.001);
		check_near("resize left: right edge fixed", l.rect.x + l.rect.width, 204, 0.001);

		const SnapResult b = snap({10, 10, 50, 167}, EdgeBottom, W, H, {}, 6);
		check_near("resize bottom: to the bottom edge", b.rect.y + b.rect.height, 180, 0.001);
		check_near("resize bottom: top fixed", b.rect.y, 10, 0.001);
	}

	/* A resize never snaps an element below the minimum size. */
	{
		const SnapResult r = snap({318, 50, 3, 40}, EdgeLeft, W, H, {}, 6, 4);
		check_true("min size: kept", r.rect.width >= 3);
	}

	/* The grid: a move lands the corner on it, a resize the dragged edge. */
	{
		const Rect moved = snap_to_grid({13, 27, 50, 33}, EdgeAll, 10);
		check_near("grid move: x", moved.x, 10, 0.001);
		check_near("grid move: y", moved.y, 30, 0.001);
		check_near("grid move: size kept", moved.width, 50, 0.001);

		const Rect sized = snap_to_grid({10, 10, 47, 33}, EdgeRight | EdgeBottom, 10);
		check_near("grid resize: right edge", sized.x + sized.width, 60, 0.001);
		check_near("grid resize: bottom edge", sized.y + sized.height, 40, 0.001);
		check_near("grid resize: left fixed", sized.x, 10, 0.001);
	}

	/* Aligning a selection to its own bounds. */
	{
		const std::vector<Rect> rects = {{10, 10, 50, 20}, {100, 40, 30, 30}, {40, 90, 80, 10}};
		const Rect bounds = bounds_of(rects);
		check_near("bounds: left", bounds.x, 10, 0.001);
		check_near("bounds: right", bounds.x + bounds.width, 130, 0.001);
		check_near("bounds: bottom", bounds.y + bounds.height, 100, 0.001);

		const std::vector<Rect> left = align_rects(rects, Align::Left, bounds);
		check_true("align left: all at 10", left[0].x == 10 && left[1].x == 10 && left[2].x == 10);
		check_near("align left: y kept", left[1].y, 40, 0.001);

		const std::vector<Rect> right = align_rects(rects, Align::Right, bounds);
		check_near("align right: right edges meet", right[1].x + right[1].width, 130, 0.001);
		check_near("align right: wide one too", right[2].x + right[2].width, 130, 0.001);

		const std::vector<Rect> middle = align_rects(rects, Align::VerticalCenter, bounds);
		check_near("align middle: centres meet", middle[0].y + middle[0].height / 2, 55, 0.5);
		check_near("align middle: tall one", middle[1].y + middle[1].height / 2, 55, 0.5);

		/* One element lines up with the widget instead. */
		const std::vector<Rect> one = align_rects({{10, 10, 100, 20}}, Align::HorizontalCenter, {0, 0, W, H});
		check_near("align one: centred in the widget", one[0].x, 270, 0.001);
	}

	/* Distributing leaves the outermost two alone and the gaps equal. */
	{
		const std::vector<Rect> rects = {{0, 0, 20, 10}, {200, 0, 20, 10}, {30, 0, 40, 10}, {100, 0, 20, 10}};
		const std::vector<Rect> spread = distribute_rects(rects, true);
		check_near("distribute: first kept", spread[0].x, 0, 0.001);
		check_near("distribute: last kept", spread[1].x, 200, 0.001);

		/* 220 wide, 100 of boxes, 3 gaps of 40. */
		check_near("distribute: second", spread[2].x, 60, 0.5);
		check_near("distribute: third", spread[3].x, 140, 0.5);

		const std::vector<Rect> two = distribute_rects({{0, 0, 10, 10}, {50, 0, 10, 10}}, true);
		check_near("distribute: two left alone", two[1].x, 50, 0.001);
	}

	return test::finish();
}
