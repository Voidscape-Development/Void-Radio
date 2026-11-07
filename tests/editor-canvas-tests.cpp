/*
 * Drives the layout editor's canvas with simulated mouse and keyboard input:
 * selecting, dragging, snapping, resizing and nudging, checked through the
 * signals the editor window listens to. Runs on Qt's offscreen platform.
 */

#include "test-check.hpp"
#include "ui/editor-canvas.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>

using test::check;
using test::check_near;
using test::check_true;
using vr::editor::CanvasElement;
using vr::editor::EditorCanvas;

namespace {

struct Recorder {
	int drag_starts = 0;
	int moves = 0;
	bool finished = false;
	QString id;
	QRectF rect;
};

void mouse(EditorCanvas &canvas, QEvent::Type type, const QPointF &at, Qt::KeyboardModifiers modifiers = {})
{
	const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
	const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
	QMouseEvent event(type, at, canvas.mapToGlobal(at), button, buttons, modifiers);
	QApplication::sendEvent(&canvas, &event);
}

/* Drags from one widget-pixel point to another, in a few steps. */
void drag(EditorCanvas &canvas, const QPointF &from, const QPointF &to, Qt::KeyboardModifiers modifiers = {})
{
	const QPointF a = canvas.toView(from);
	const QPointF b = canvas.toView(to);
	mouse(canvas, QEvent::MouseButtonPress, a, modifiers);
	for (int i = 1; i <= 5; i++)
		mouse(canvas, QEvent::MouseMove, a + (b - a) * (i / 5.0), modifiers);
	mouse(canvas, QEvent::MouseButtonRelease, b, modifiers);
}

QVector<CanvasElement> elements()
{
	return {
		{"art_", "Album art", QRectF(20, 20, 140, 140), true},
		{"t0_", "Line 1", QRectF(180, 40, 400, 40), true},
		{"bar_", "Progress bar", QRectF(180, 140, 400, 8), true},
		{"t1_", "Line 2", QRectF(180, 90, 400, 30), false},
	};
}

} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);

	EditorCanvas canvas;
	canvas.resize(820, 320);
	canvas.setCanvasSize(QSize(640, 180));
	canvas.setElements(elements());
	canvas.show();

	Recorder rec;
	QObject::connect(&canvas, &EditorCanvas::dragStarted, [&](const QString &) { rec.drag_starts++; });
	QObject::connect(&canvas, &EditorCanvas::elementMoved,
			 [&](const QString &id, const QRectF &rect, bool finished) {
				 rec.moves++;
				 rec.id = id;
				 rec.rect = rect;
				 rec.finished = finished;
			 });

	/* A click selects without moving anything. */
	{
		const QPointF at = canvas.toView(QPointF(90, 90));
		mouse(canvas, QEvent::MouseButtonPress, at);
		mouse(canvas, QEvent::MouseButtonRelease, at);
		check_true("click selects the art", canvas.selected() == "art_");
		check("click is not a drag", rec.drag_starts, 0);
		check("click moves nothing", rec.moves, 0);
	}

	/* A drag moves it, announcing itself once, and ends finished. */
	{
		drag(canvas, QPointF(90, 90), QPointF(130, 90));
		check("drag starts once", rec.drag_starts, 1);
		check_true("drag reports moves", rec.moves > 1);
		check_true("drag ends finished", rec.finished);
		check_near("drag moved right by 40", rec.rect.x(), 60, 0.6);
		check_near("drag kept y", rec.rect.y(), 20, 0.6);
		check_near("drag kept width", rec.rect.width(), 140, 0.01);
	}

	/* Close to the edge it snaps; with Alt held it does not. */
	{
		canvas.setElements(elements());
		drag(canvas, QPointF(90, 90), QPointF(72, 90));
		check_near("snaps to the left edge", rec.rect.x(), 0, 0.01);

		canvas.setElements(elements());
		drag(canvas, QPointF(90, 90), QPointF(72, 90), Qt::AltModifier);
		check_near("Alt turns snapping off", rec.rect.x(), 2, 0.6);
	}

	/* The corner handle resizes. */
	{
		canvas.setElements(elements());
		canvas.setSelected("art_");
		drag(canvas, QPointF(160, 160), QPointF(130, 120), Qt::AltModifier);
		check_true("resize keeps the corner it did not drag", rec.rect.topLeft() == QPointF(20, 20));
		check_near("resize narrows", rec.rect.width(), 110, 0.6);
		check_near("resize shortens", rec.rect.height(), 100, 0.6);
	}

	/* Hidden elements cannot be picked; what is under them can. */
	{
		canvas.setElements(elements());
		const QPointF at = canvas.toView(QPointF(300, 105));
		mouse(canvas, QEvent::MouseButtonPress, at);
		mouse(canvas, QEvent::MouseButtonRelease, at);
		check_true("hidden line is not selected", canvas.selected().isEmpty());
	}

	/* Arrow keys nudge the selection, Shift by ten. */
	{
		canvas.setElements(elements());
		canvas.setSelected("t0_");
		const int starts = rec.drag_starts;

		QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
		QApplication::sendEvent(&canvas, &right);
		check_near("arrow nudges a pixel", rec.rect.x(), 181, 0.01);
		check_true("nudge is finished at once", rec.finished && rec.id == "t0_");
		check("nudge announces itself", rec.drag_starts, starts + 1);

		QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
		QApplication::sendEvent(&canvas, &down);
		check_near("shift-arrow nudges ten", rec.rect.y(), 50, 0.01);

		QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QApplication::sendEvent(&canvas, &escape);
		check_true("escape deselects", canvas.selected().isEmpty());
	}

	/* A picture of the canvas, for looking at by eye. */
	{
		QImage preview(640, 180, QImage::Format_RGBA8888);
		preview.fill(QColor(40, 30, 90, 230));
		canvas.setPreview(preview);
		canvas.setElements(elements());
		canvas.setSelected("t0_");
		const char *out = getenv("CANVAS_SHOT");
		if (out)
			canvas.grab().save(QString::fromUtf8(out));
	}

	return test::finish();
}
