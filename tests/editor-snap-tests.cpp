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

	return test::finish();
}
