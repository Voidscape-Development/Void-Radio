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

#include "ui/editor-canvas.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vr::editor {

namespace {

constexpr double MARGIN = 28.0;
constexpr double HANDLE = 8.0;
constexpr double SNAP_PX = 6.0;    /* snapping distance, in view pixels */
constexpr double DRAG_START = 3.0; /* movement before a press becomes a drag */
constexpr float MIN_SIZE = 4.0f;
constexpr double MIN_ZOOM = 0.1;
constexpr double MAX_ZOOM = 16.0;

/* Corners first, then edges, in the order handleRects() returns them. */
const unsigned HANDLE_EDGES[8] = {
	EdgeLeft | EdgeTop,
	EdgeRight | EdgeTop,
	EdgeRight | EdgeBottom,
	EdgeLeft | EdgeBottom,
	EdgeTop,
	EdgeRight,
	EdgeBottom,
	EdgeLeft,
};

const QColor GUIDE_COLOR(255, 72, 168);

Rect to_rect(const QRectF &r)
{
	return Rect{(float)r.x(), (float)r.y(), (float)r.width(), (float)r.height()};
}

QRectF to_qrect(const Rect &r)
{
	return QRectF(r.x, r.y, r.width, r.height);
}

bool adds_to_selection(Qt::KeyboardModifiers modifiers)
{
	return modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
}

/* A small padlock, for locked elements. */
void draw_lock(QPainter &p, const QPointF &at, const QColor &color)
{
	p.save();
	p.setPen(QPen(color, 1.4));
	p.setBrush(Qt::NoBrush);
	p.drawArc(QRectF(at.x() + 2.0, at.y(), 6.0, 7.0), 0, 180 * 16);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(QRectF(at.x(), at.y() + 4.0, 10.0, 7.0), 1.5, 1.5);
	p.restore();
}

} // namespace

EditorCanvas::EditorCanvas(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setMinimumSize(320, 200);
}

void EditorCanvas::setCanvasSize(const QSize &size)
{
	if (size == canvas_size_ || size.isEmpty())
		return;

	canvas_size_ = size;
	update();
}

void EditorCanvas::setPreview(const QImage &image)
{
	preview_ = image;
	update();
}

void EditorCanvas::setElements(const QVector<CanvasElement> &elements)
{
	/* The elements being dragged keep the positions the mouse gave them;
	 * the settings catch up when the drag ends. */
	QVector<CanvasMove> dragged;
	if (drag_.mode == Mode::Moving) {
		for (const CanvasMove &move : drag_.start) {
			const int index = indexOf(move.id);
			if (index >= 0)
				dragged.append({move.id, elements_[index].rect});
		}
	}

	elements_ = elements;

	for (const CanvasMove &move : dragged) {
		const int index = indexOf(move.id);
		if (index >= 0)
			elements_[index].rect = move.rect;
	}

	/* Anything that went away cannot stay selected. */
	QStringList kept;
	for (const QString &id : selection_)
		if (indexOf(id) >= 0)
			kept.append(id);
	if (kept != selection_)
		applySelection(kept, kept.contains(primary_) ? primary_ : (kept.isEmpty() ? QString() : kept.last()));

	update();
}

void EditorCanvas::setSelected(const QString &id)
{
	applySelection(id.isEmpty() ? QStringList() : QStringList{id}, id);
}

void EditorCanvas::setSelection(const QStringList &ids)
{
	applySelection(ids, ids.contains(primary_) ? primary_ : (ids.isEmpty() ? QString() : ids.last()));
}

void EditorCanvas::applySelection(const QStringList &ids, const QString &primary)
{
	if (ids == selection_ && primary == primary_)
		return;

	selection_ = ids;
	primary_ = primary;
	update();

	/* Sent whenever the set changes too, so a listener showing the whole
	 * selection keeps up even when the primary element stays the same. */
	emit selectionChanged(primary_);
}

void EditorCanvas::setZoom(double zoom)
{
	zoomAround(zoom, QPointF(width() * 0.5, height() * 0.5));
}

void EditorCanvas::zoomToFit()
{
	zoom_ = 1.0;
	pan_ = QPointF();
	update();
	emit zoomChanged(zoom_);
}

void EditorCanvas::zoomAround(double zoom, const QPointF &view_point)
{
	zoom = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
	if (std::abs(zoom - zoom_) < 1e-9)
		return;

	/* Whatever is under the point stays under it. */
	const QPointF anchor = fromView(view_point);
	zoom_ = zoom;
	pan_ += view_point - toView(anchor);

	update();
	emit zoomChanged(zoom_);
}

void EditorCanvas::setGrid(bool show, int size, bool snap)
{
	grid_show_ = show;
	grid_size_ = std::max(2, size);
	grid_snap_ = snap;
	update();
}

/* ------------------------------------------------------------------------- */
/* Geometry                                                                   */
/* ------------------------------------------------------------------------- */

double EditorCanvas::fitScale() const
{
	const double sx = (width() - 2.0 * MARGIN) / std::max(1, canvas_size_.width());
	const double sy = (height() - 2.0 * MARGIN) / std::max(1, canvas_size_.height());
	return std::max(0.05, std::min(sx, sy));
}

double EditorCanvas::scale() const
{
	return fitScale() * zoom_;
}

QPointF EditorCanvas::origin() const
{
	const double s = scale();
	return QPointF((width() - canvas_size_.width() * s) * 0.5, (height() - canvas_size_.height() * s) * 0.5) + pan_;
}

QPointF EditorCanvas::toView(const QPointF &point) const
{
	return origin() + point * scale();
}

QPointF EditorCanvas::fromView(const QPointF &point) const
{
	return (point - origin()) / scale();
}

QRectF EditorCanvas::viewRect(const QRectF &rect) const
{
	return QRectF(toView(rect.topLeft()), rect.size() * scale());
}

int EditorCanvas::indexOf(const QString &id) const
{
	for (int i = 0; i < elements_.size(); i++)
		if (elements_[i].id == id)
			return i;
	return -1;
}

bool EditorCanvas::canMove(const QString &id) const
{
	const int index = indexOf(id);
	return index >= 0 && elements_[index].visible && !elements_[index].locked;
}

QString EditorCanvas::elementAt(const QPointF &view_point) const
{
	/* Later elements draw on top, so they are hit first. The selected one
	 * wins over anything it overlaps, so it can always be grabbed again. */
	const int primary = indexOf(primary_);
	if (primary >= 0 && elements_[primary].visible && viewRect(elements_[primary].rect).contains(view_point))
		return primary_;

	for (int i = (int)elements_.size() - 1; i >= 0; i--) {
		if (elements_[i].visible && viewRect(elements_[i].rect).contains(view_point))
			return elements_[i].id;
	}

	return QString();
}

QVector<QRectF> EditorCanvas::handleRects(const QRectF &r) const
{
	const double h = HANDLE;
	const auto at = [h](double x, double y) {
		return QRectF(x - h / 2, y - h / 2, h, h);
	};

	const double cx = r.center().x();
	const double cy = r.center().y();

	return {at(r.left(), r.top()), at(r.right(), r.top()), at(r.right(), r.bottom()), at(r.left(), r.bottom()),
		at(cx, r.top()),       at(r.right(), cy),      at(cx, r.bottom()),        at(r.left(), cy)};
}

unsigned EditorCanvas::handleAt(const QPointF &view_point) const
{
	/* Handles belong to a single, movable selection. */
	if (selection_.size() != 1 || !canMove(primary_))
		return 0;

	const QVector<QRectF> handles = handleRects(viewRect(elements_[indexOf(primary_)].rect));
	for (int i = 0; i < handles.size(); i++) {
		if (handles[i].adjusted(-3, -3, 3, 3).contains(view_point))
			return HANDLE_EDGES[i];
	}

	return 0;
}

void EditorCanvas::updateCursor(const QPointF &view_point)
{
	const unsigned edges = handleAt(view_point);
	const QString over = elementAt(view_point);

	if (edges == (EdgeLeft | EdgeTop) || edges == (EdgeRight | EdgeBottom))
		setCursor(Qt::SizeFDiagCursor);
	else if (edges == (EdgeRight | EdgeTop) || edges == (EdgeLeft | EdgeBottom))
		setCursor(Qt::SizeBDiagCursor);
	else if (edges == EdgeLeft || edges == EdgeRight)
		setCursor(Qt::SizeHorCursor);
	else if (edges == EdgeTop || edges == EdgeBottom)
		setCursor(Qt::SizeVerCursor);
	else if (!over.isEmpty() && canMove(over))
		setCursor(Qt::SizeAllCursor);
	else
		unsetCursor();
}

/* ------------------------------------------------------------------------- */
/* Painting                                                                   */
/* ------------------------------------------------------------------------- */

void EditorCanvas::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);

	p.fillRect(rect(), palette().color(QPalette::Base).darker(115));

	const QRectF frame = viewRect(QRectF(QPointF(0, 0), QSizeF(canvas_size_)));

	/* Transparency shows as a checkerboard, the way OBS shows it. */
	p.save();
	p.setClipRect(frame.intersected(QRectF(rect())));
	const int cell = 10;
	const QColor light(58, 60, 68), dark(46, 48, 55);
	const QRect visible = frame.intersected(QRectF(rect())).toAlignedRect();
	for (int y = visible.top(); y < visible.bottom() + 1; y += cell)
		for (int x = visible.left(); x < visible.right() + 1; x += cell)
			p.fillRect(QRect(x, y, cell, cell),
				   (((x - (int)frame.left()) / cell + (y - (int)frame.top()) / cell) % 2) ? light
													  : dark);

	if (!preview_.isNull())
		p.drawImage(frame, preview_);

	/* The grid, when it is wide enough apart to be worth drawing. */
	const double grid_view = grid_size_ * scale();
	if (grid_show_ && grid_view >= 4.0) {
		p.setPen(QPen(QColor(255, 255, 255, 34), 1.0));
		for (double x = 0; x <= canvas_size_.width(); x += grid_size_) {
			const double vx = toView(QPointF(x, 0)).x();
			p.drawLine(QPointF(vx, frame.top()), QPointF(vx, frame.bottom()));
		}
		for (double y = 0; y <= canvas_size_.height(); y += grid_size_) {
			const double vy = toView(QPointF(0, y)).y();
			p.drawLine(QPointF(frame.left(), vy), QPointF(frame.right(), vy));
		}
	}
	p.restore();

	p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
	p.setBrush(Qt::NoBrush);
	p.drawRect(frame.adjusted(-0.5, -0.5, 0.5, 0.5));

	const QColor highlight = palette().color(QPalette::Highlight);

	/* Every element faintly, so the structure of the card is visible. */
	for (const CanvasElement &element : elements_) {
		if (!element.visible || isSelected(element.id))
			continue;

		const QRectF r = viewRect(element.rect);
		const bool hovered = element.id == hovered_;

		QColor line = hovered ? highlight : QColor(255, 255, 255, 70);
		QPen pen(line, hovered ? 1.5 : 1.0, hovered ? Qt::SolidLine : Qt::DashLine);
		p.setPen(pen);
		p.drawRect(r);

		if (element.locked)
			draw_lock(p, r.topRight() + QPointF(-14, 4), QColor(255, 255, 255, 140));
	}

	/* Snap guides while dragging. */
	p.setPen(QPen(GUIDE_COLOR, 1.0));
	for (const Guide &guide : guides_) {
		if (guide.vertical) {
			const double x = toView(QPointF(guide.position, 0)).x();
			p.drawLine(QPointF(x, frame.top() - 12), QPointF(x, frame.bottom() + 12));
		} else {
			const double y = toView(QPointF(0, guide.position)).y();
			p.drawLine(QPointF(frame.left() - 12, y), QPointF(frame.right() + 12, y));
		}
	}

	/* The selection; handles when it is one element that can move. */
	for (const QString &id : selection_) {
		const int index = indexOf(id);
		if (index < 0 || !elements_[index].visible)
			continue;

		const QRectF r = viewRect(elements_[index].rect);
		p.setPen(QPen(highlight, id == primary_ ? 1.75 : 1.25));
		p.setBrush(Qt::NoBrush);
		p.drawRect(r);

		if (elements_[index].locked)
			draw_lock(p, r.topRight() + QPointF(-14, 4), highlight);
	}

	if (selection_.size() == 1 && canMove(primary_)) {
		p.setPen(QPen(highlight, 1.25));
		p.setBrush(Qt::white);
		for (const QRectF &handle : handleRects(viewRect(elements_[indexOf(primary_)].rect)))
			p.drawRect(handle);
	}

	/* The selection band. */
	if (drag_.mode == Mode::Band && !band_.isEmpty()) {
		QColor fill = highlight;
		fill.setAlpha(40);
		p.setPen(QPen(highlight, 1.0));
		p.setBrush(fill);
		p.drawRect(band_);
	}

	/* A label with the name and size of what is selected or hovered. */
	const int primary = indexOf(primary_);
	const int labelled = primary >= 0 ? primary : indexOf(hovered_);

	if (labelled >= 0 && elements_[labelled].visible) {
		const CanvasElement &element = elements_[labelled];
		const QRectF r = viewRect(element.rect);
		QString text = QStringLiteral("%1  %2 × %3")
				       .arg(element.label)
				       .arg(std::lround(element.rect.width()))
				       .arg(std::lround(element.rect.height()));
		if (selection_.size() > 1)
			text += QStringLiteral("  (+%1)").arg(selection_.size() - 1);

		QFont font = p.font();
		font.setPointSizeF(std::max(7.5, font.pointSizeF() * 0.85));
		p.setFont(font);

		const QFontMetricsF metrics(font);
		const QSizeF size(metrics.horizontalAdvance(text) + 12, metrics.height() + 4);
		QPointF at(r.left(), r.top() - size.height() - 4);
		if (at.y() < 2)
			at.setY(r.bottom() + 4);

		const QRectF chip(at, size);
		p.setPen(Qt::NoPen);
		p.setBrush(highlight);
		p.drawRoundedRect(chip, 4, 4);
		p.setPen(palette().color(QPalette::HighlightedText));
		p.drawText(chip, Qt::AlignCenter, text);
	}

	/* The zoom, when it is not simply fitted. */
	if (std::abs(zoom_ - 1.0) > 1e-6) {
		const QString text = QStringLiteral("%1%").arg(std::lround(zoom_ * 100.0));
		p.setPen(QColor(255, 255, 255, 150));
		p.drawText(QRectF(rect()).adjusted(8, 4, -8, -4), Qt::AlignRight | Qt::AlignBottom, text);
	}
}

/* ------------------------------------------------------------------------- */
/* Interaction                                                                */
/* ------------------------------------------------------------------------- */

void EditorCanvas::emitMoves(const QVector<CanvasMove> &moves, bool finished)
{
	if (moves.isEmpty())
		return;

	emit elementsMoved(moves, finished);
	for (const CanvasMove &move : moves)
		emit elementMoved(move.id, move.rect, finished);
}

void EditorCanvas::mousePressEvent(QMouseEvent *event)
{
	const QPointF pos = event->position();

	if (event->button() == Qt::MiddleButton) {
		drag_ = Drag();
		drag_.mode = Mode::Pan;
		drag_.start_view = pos;
		drag_.start_pan = pan_;
		setCursor(Qt::ClosedHandCursor);
		return;
	}

	if (event->button() != Qt::LeftButton)
		return;

	drag_ = Drag();
	drag_.start_view = pos;
	drag_.start_mouse = fromView(pos);

	const unsigned edges = handleAt(pos);

	if (edges) {
		drag_.mode = Mode::Pending;
		drag_.id = primary_;
		drag_.edges = edges;
		drag_.start.append({primary_, elements_[indexOf(primary_)].rect});
		return;
	}

	const QString id = elementAt(pos);

	if (id.isEmpty()) {
		drag_.mode = Mode::Band;
		drag_.additive = adds_to_selection(event->modifiers());
		band_ = QRectF();
		if (!drag_.additive)
			applySelection({}, QString());
		return;
	}

	if (adds_to_selection(event->modifiers())) {
		QStringList ids = selection_;
		if (ids.contains(id)) {
			ids.removeAll(id);
			applySelection(ids, id == primary_ ? (ids.isEmpty() ? QString() : ids.last()) : primary_);
			return;
		}
		ids.append(id);
		applySelection(ids, id);
	} else if (isSelected(id)) {
		/* Part of the selection already: keep the rest, so the whole of
		 * it can be dragged, and narrow it down on release instead. */
		drag_.reselect = selection_.size() > 1;
		applySelection(selection_, id);
	} else {
		applySelection({id}, id);
	}

	drag_.mode = Mode::Pending;
	drag_.id = id;
	drag_.edges = EdgeAll;
	for (const QString &selected : selection_) {
		if (canMove(selected))
			drag_.start.append({selected, elements_[indexOf(selected)].rect});
	}
}

void EditorCanvas::mouseMoveEvent(QMouseEvent *event)
{
	const QPointF pos = event->position();

	switch (drag_.mode) {
	case Mode::None: {
		const QString hovered = elementAt(pos);
		if (hovered != hovered_) {
			hovered_ = hovered;
			update();
		}
		updateCursor(pos);
		return;
	}

	case Mode::Pan:
		pan_ = drag_.start_pan + (pos - drag_.start_view);
		update();
		return;

	case Mode::Band:
		band_ = QRectF(drag_.start_view, pos).normalized();
		update();
		return;

	case Mode::Pending:
		if (QLineF(drag_.start_view, pos).length() < DRAG_START || drag_.start.isEmpty())
			return;

		/* The editor may switch to free placement here, which hands the
		 * canvas a fresh set of elements; the drag carries on from
		 * where the elements were. */
		drag_.mode = Mode::Moving;
		drag_.reselect = false;
		emit dragStarted(drag_.id);
		break;

	case Mode::Moving:
		break;
	}

	const QPointF delta = fromView(pos) - drag_.start_mouse;
	const bool snapping = !(event->modifiers() & Qt::AltModifier);
	const unsigned e = drag_.edges;

	QStringList moving;
	std::vector<Rect> starts;
	for (const CanvasMove &move : drag_.start) {
		moving.append(move.id);
		starts.push_back(to_rect(move.rect));
	}

	std::vector<Rect> others;
	for (const CanvasElement &element : elements_)
		if (element.visible && !moving.contains(element.id))
			others.push_back(to_rect(element.rect));

	guides_.clear();
	QVector<CanvasMove> moves;

	if (e == EdgeAll) {
		/* A group moves as one box: snapping lines up the edges of the
		 * whole selection, then every member moves by the same amount. */
		const Rect start = bounds_of(starts);
		Rect r = start;
		r.x += (float)delta.x();
		r.y += (float)delta.y();

		/* The grid, when it is snapped to, replaces snapping to other
		 * elements: two sets of targets fight each other. */
		if (snapping && grid_snap_) {
			r = snap_to_grid(r, EdgeAll, (float)grid_size_, MIN_SIZE);
		} else if (snapping) {
			const SnapResult snapped = snap(r, EdgeAll, (float)canvas_size_.width(),
							(float)canvas_size_.height(), others,
							(float)(SNAP_PX / scale()), MIN_SIZE);
			r = snapped.rect;
			guides_ = snapped.guides;
		}

		const QPointF shift(r.x - start.x, r.y - start.y);
		for (const CanvasMove &move : drag_.start)
			moves.append({move.id, move.rect.translated(shift)});
	} else {
		QRectF r = drag_.start.first().rect;

		if (e & EdgeLeft)
			r.setLeft(std::min(r.left() + delta.x(), r.right() - MIN_SIZE));
		if (e & EdgeRight)
			r.setRight(std::max(r.right() + delta.x(), r.left() + MIN_SIZE));
		if (e & EdgeTop)
			r.setTop(std::min(r.top() + delta.y(), r.bottom() - MIN_SIZE));
		if (e & EdgeBottom)
			r.setBottom(std::max(r.bottom() + delta.y(), r.top() + MIN_SIZE));

		if (snapping && grid_snap_) {
			r = to_qrect(snap_to_grid(to_rect(r), e, (float)grid_size_, MIN_SIZE));
		} else if (snapping) {
			const SnapResult snapped = snap(to_rect(r), e, (float)canvas_size_.width(),
							(float)canvas_size_.height(), others,
							(float)(SNAP_PX / scale()), MIN_SIZE);
			r = to_qrect(snapped.rect);
			guides_ = snapped.guides;
		}

		moves.append({drag_.start.first().id, r});
	}

	for (const CanvasMove &move : moves) {
		const int index = indexOf(move.id);
		if (index >= 0)
			elements_[index].rect = move.rect;
	}

	update();
	emitMoves(moves, false);
}

void EditorCanvas::mouseReleaseEvent(QMouseEvent *event)
{
	if (drag_.mode == Mode::Pan && event->button() == Qt::MiddleButton) {
		drag_ = Drag();
		updateCursor(event->position());
		return;
	}

	if (event->button() != Qt::LeftButton)
		return;

	if (drag_.mode == Mode::Moving) {
		QVector<CanvasMove> moves;
		for (const CanvasMove &move : drag_.start) {
			const int index = indexOf(move.id);
			if (index >= 0)
				moves.append({move.id, elements_[index].rect});
		}
		emitMoves(moves, true);
	} else if (drag_.mode == Mode::Band) {
		QStringList ids = drag_.additive ? selection_ : QStringList();
		QString primary = drag_.additive ? primary_ : QString();

		if (band_.width() >= 2.0 || band_.height() >= 2.0) {
			for (const CanvasElement &element : elements_) {
				if (!element.visible || ids.contains(element.id))
					continue;
				if (band_.intersects(viewRect(element.rect))) {
					ids.append(element.id);
					primary = element.id;
				}
			}
		}

		applySelection(ids, primary);
	} else if (drag_.mode == Mode::Pending && drag_.reselect) {
		applySelection({drag_.id}, drag_.id);
	}

	drag_ = Drag();
	band_ = QRectF();
	guides_.clear();
	update();
	updateCursor(event->position());
}

void EditorCanvas::wheelEvent(QWheelEvent *event)
{
	const QPoint steps = event->angleDelta();

	if (event->modifiers() & Qt::ControlModifier) {
		const double notches = steps.y() / 120.0;
		zoomAround(zoom_ * std::pow(1.15, notches), event->position());
	} else {
		/* Scrolling: Shift turns the wheel sideways, as in most apps. */
		QPointF by(steps.x() / 2.0, steps.y() / 2.0);
		if (event->modifiers() & Qt::ShiftModifier)
			by = QPointF(by.y(), by.x());
		pan_ += by;
		update();
	}

	event->accept();
}

void EditorCanvas::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape) {
		applySelection({}, QString());
		return;
	}

	if (event->matches(QKeySequence::SelectAll)) {
		QStringList ids;
		for (const CanvasElement &element : elements_)
			if (element.visible)
				ids.append(element.id);
		applySelection(ids, ids.contains(primary_) ? primary_ : (ids.isEmpty() ? QString() : ids.last()));
		return;
	}

	QPointF step;
	const double amount = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;

	switch (event->key()) {
	case Qt::Key_Left:
		step = QPointF(-amount, 0);
		break;
	case Qt::Key_Right:
		step = QPointF(amount, 0);
		break;
	case Qt::Key_Up:
		step = QPointF(0, -amount);
		break;
	case Qt::Key_Down:
		step = QPointF(0, amount);
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}

	QStringList moving;
	for (const QString &id : selection_)
		if (canMove(id))
			moving.append(id);

	if (moving.isEmpty())
		return;

	emit dragStarted(moving.contains(primary_) ? primary_ : moving.first());

	QVector<CanvasMove> moves;
	for (const QString &id : moving) {
		const int index = indexOf(id);
		if (index < 0)
			continue;
		elements_[index].rect.translate(step);
		moves.append({id, elements_[index].rect});
	}

	update();
	emitMoves(moves, true);
}

void EditorCanvas::leaveEvent(QEvent *event)
{
	hovered_.clear();
	update();
	QWidget::leaveEvent(event);
}

} // namespace vr::editor
