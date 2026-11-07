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

#include <algorithm>
#include <cmath>

namespace vr::editor {

namespace {

constexpr double MARGIN = 28.0;
constexpr double HANDLE = 8.0;
constexpr double SNAP_PX = 6.0;    /* snapping distance, in view pixels */
constexpr double DRAG_START = 3.0; /* movement before a press becomes a drag */
constexpr float MIN_SIZE = 4.0f;

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
	/* The element being dragged keeps the position the mouse gave it; the
	 * settings catch up when the drag ends. */
	QRectF dragged;
	const int dragging = drag_.active ? indexOf(drag_.id) : -1;
	if (dragging >= 0)
		dragged = elements_[dragging].rect;

	elements_ = elements;

	const int now = dragging >= 0 ? indexOf(drag_.id) : -1;
	if (now >= 0)
		elements_[now].rect = dragged;

	update();
}

void EditorCanvas::setSelected(const QString &id)
{
	if (id == selected_)
		return;

	selected_ = id;
	update();
	emit selectionChanged(selected_);
}

/* ------------------------------------------------------------------------- */
/* Geometry                                                                   */
/* ------------------------------------------------------------------------- */

double EditorCanvas::scale() const
{
	const double sx = (width() - 2.0 * MARGIN) / std::max(1, canvas_size_.width());
	const double sy = (height() - 2.0 * MARGIN) / std::max(1, canvas_size_.height());
	return std::max(0.05, std::min(sx, sy));
}

QPointF EditorCanvas::origin() const
{
	const double s = scale();
	return QPointF((width() - canvas_size_.width() * s) * 0.5, (height() - canvas_size_.height() * s) * 0.5);
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

QString EditorCanvas::elementAt(const QPointF &view_point) const
{
	/* Later elements draw on top, so they are hit first. The selected one
	 * wins over anything it overlaps, so it can always be grabbed again. */
	const int selected = indexOf(selected_);
	if (selected >= 0 && elements_[selected].visible && viewRect(elements_[selected].rect).contains(view_point))
		return selected_;

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
	const int selected = indexOf(selected_);
	if (selected < 0 || !elements_[selected].visible)
		return 0;

	const QVector<QRectF> handles = handleRects(viewRect(elements_[selected].rect));
	for (int i = 0; i < handles.size(); i++) {
		if (handles[i].adjusted(-3, -3, 3, 3).contains(view_point))
			return HANDLE_EDGES[i];
	}

	return 0;
}

void EditorCanvas::updateCursor(const QPointF &view_point)
{
	const unsigned edges = handleAt(view_point);

	if (edges == (EdgeLeft | EdgeTop) || edges == (EdgeRight | EdgeBottom))
		setCursor(Qt::SizeFDiagCursor);
	else if (edges == (EdgeRight | EdgeTop) || edges == (EdgeLeft | EdgeBottom))
		setCursor(Qt::SizeBDiagCursor);
	else if (edges == EdgeLeft || edges == EdgeRight)
		setCursor(Qt::SizeHorCursor);
	else if (edges == EdgeTop || edges == EdgeBottom)
		setCursor(Qt::SizeVerCursor);
	else if (!elementAt(view_point).isEmpty())
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
	p.setClipRect(frame);
	const int cell = 10;
	const QColor light(58, 60, 68), dark(46, 48, 55);
	for (int y = (int)frame.top(); y < frame.bottom(); y += cell)
		for (int x = (int)frame.left(); x < frame.right(); x += cell)
			p.fillRect(QRect(x, y, cell, cell),
				   (((x - (int)frame.left()) / cell + (y - (int)frame.top()) / cell) % 2) ? light
													  : dark);

	if (!preview_.isNull())
		p.drawImage(frame, preview_);
	p.restore();

	p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
	p.setBrush(Qt::NoBrush);
	p.drawRect(frame.adjusted(-0.5, -0.5, 0.5, 0.5));

	const QColor highlight = palette().color(QPalette::Highlight);

	/* Every element faintly, so the structure of the card is visible. */
	for (const CanvasElement &element : elements_) {
		if (!element.visible || element.id == selected_)
			continue;

		const QRectF r = viewRect(element.rect);
		const bool hovered = element.id == hovered_;

		QColor line = hovered ? highlight : QColor(255, 255, 255, 70);
		QPen pen(line, hovered ? 1.5 : 1.0, hovered ? Qt::SolidLine : Qt::DashLine);
		p.setPen(pen);
		p.drawRect(r);
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

	/* The selection, its handles and a label with its name and size. */
	const int selected = indexOf(selected_);
	const int labelled = selected >= 0 ? selected : indexOf(hovered_);

	if (selected >= 0 && elements_[selected].visible) {
		const QRectF r = viewRect(elements_[selected].rect);

		p.setPen(QPen(highlight, 1.5));
		p.drawRect(r);

		p.setPen(QPen(highlight, 1.25));
		p.setBrush(Qt::white);
		for (const QRectF &handle : handleRects(r))
			p.drawRect(handle);
	}

	if (labelled >= 0 && elements_[labelled].visible) {
		const CanvasElement &element = elements_[labelled];
		const QRectF r = viewRect(element.rect);
		const QString text = QStringLiteral("%1  %2 × %3")
					     .arg(element.label)
					     .arg(std::lround(element.rect.width()))
					     .arg(std::lround(element.rect.height()));

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
}

/* ------------------------------------------------------------------------- */
/* Interaction                                                                */
/* ------------------------------------------------------------------------- */

void EditorCanvas::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	const QPointF pos = event->position();
	unsigned edges = handleAt(pos);
	QString id = edges ? selected_ : elementAt(pos);

	if (!edges && !id.isEmpty())
		edges = EdgeAll;

	setSelected(id);

	drag_ = Drag();
	if (!id.isEmpty()) {
		drag_.id = id;
		drag_.edges = edges;
		drag_.start_rect = elements_[indexOf(id)].rect;
		drag_.start_mouse = fromView(pos);
	}
}

void EditorCanvas::mouseMoveEvent(QMouseEvent *event)
{
	const QPointF pos = event->position();

	if (drag_.id.isEmpty()) {
		const QString hovered = elementAt(pos);
		if (hovered != hovered_) {
			hovered_ = hovered;
			update();
		}
		updateCursor(pos);
		return;
	}

	const QPointF delta = fromView(pos) - drag_.start_mouse;

	if (!drag_.active) {
		if (QLineF(QPointF(), delta * scale()).length() < DRAG_START)
			return;

		/* The editor may switch to free placement here, which hands the
		 * canvas a fresh set of elements; the drag carries on from
		 * where the element was. */
		emit dragStarted(drag_.id);
		drag_.active = true;
	}

	QRectF r = drag_.start_rect;
	const unsigned e = drag_.edges;

	if (e == EdgeAll) {
		r.translate(delta);
	} else {
		if (e & EdgeLeft)
			r.setLeft(std::min(r.left() + delta.x(), r.right() - MIN_SIZE));
		if (e & EdgeRight)
			r.setRight(std::max(r.right() + delta.x(), r.left() + MIN_SIZE));
		if (e & EdgeTop)
			r.setTop(std::min(r.top() + delta.y(), r.bottom() - MIN_SIZE));
		if (e & EdgeBottom)
			r.setBottom(std::max(r.bottom() + delta.y(), r.top() + MIN_SIZE));
	}

	guides_.clear();

	if (!(event->modifiers() & Qt::AltModifier)) {
		std::vector<Rect> others;
		for (const CanvasElement &element : elements_)
			if (element.visible && element.id != drag_.id)
				others.push_back(to_rect(element.rect));

		const SnapResult snapped = snap(to_rect(r), e, (float)canvas_size_.width(),
						(float)canvas_size_.height(), others, (float)(SNAP_PX / scale()),
						MIN_SIZE);
		r = to_qrect(snapped.rect);
		guides_ = snapped.guides;
	}

	const int index = indexOf(drag_.id);
	if (index >= 0)
		elements_[index].rect = r;

	update();
	emit elementMoved(drag_.id, r, false);
}

void EditorCanvas::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	if (drag_.active) {
		const int index = indexOf(drag_.id);
		if (index >= 0)
			emit elementMoved(drag_.id, elements_[index].rect, true);
	}

	drag_ = Drag();
	guides_.clear();
	update();
	updateCursor(event->position());
}

void EditorCanvas::keyPressEvent(QKeyEvent *event)
{
	const int index = indexOf(selected_);

	if (event->key() == Qt::Key_Escape) {
		setSelected(QString());
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

	if (index < 0 || !elements_[index].visible)
		return;

	emit dragStarted(selected_);

	const int now = indexOf(selected_);
	if (now < 0)
		return;

	elements_[now].rect.translate(step);
	update();
	emit elementMoved(selected_, elements_[now].rect, true);
}

void EditorCanvas::leaveEvent(QEvent *event)
{
	hovered_.clear();
	update();
	QWidget::leaveEvent(event);
}

} // namespace vr::editor
