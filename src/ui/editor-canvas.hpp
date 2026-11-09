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

#pragma once

#include "ui/editor-snap.hpp"

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace vr::editor {

struct CanvasElement {
	QString id;
	QString label;
	QRectF rect; /* widget pixels, from the top left */
	bool visible = true;
	bool locked = false; /* can be selected, not dragged */
};

struct CanvasMove {
	QString id;
	QRectF rect;
};

/*
 * The middle of the layout editor: the widget as OBS draws it, with every
 * element outlined, and handles to move and resize the selection.
 *
 * Click to select, Shift or Ctrl click to add to the selection or take away
 * from it, or drag across empty space to select everything the band touches.
 * Dragging any selected element moves the whole selection; a single element
 * also gets handles to resize it. Ctrl and the scroll wheel zoom, the wheel on
 * its own scrolls, and the middle button drags the view around.
 *
 * It knows nothing about OBS. It shows what it is given and reports what the
 * user does in widget pixels; the editor window turns that into settings.
 */
class EditorCanvas : public QWidget {
	Q_OBJECT

public:
	explicit EditorCanvas(QWidget *parent = nullptr);

	void setCanvasSize(const QSize &size);
	void setPreview(const QImage &image);
	void setElements(const QVector<CanvasElement> &elements);

	/* Replaces the selection with one element, or none. */
	void setSelected(const QString &id);
	void setSelection(const QStringList &ids);

	/* The element most recently picked, which the inspector shows. */
	QString selected() const { return primary_; }
	QStringList selection() const { return selection_; }

	/* 1 fits the widget to the view; 2 is twice that. */
	void setZoom(double zoom);
	double zoom() const { return zoom_; }
	void zoomToFit();

	/* How many view pixels one widget pixel takes up at the current zoom. */
	double pixelScale() const { return scale(); }

	void setGrid(bool show, int size, bool snap);

	/* Widget pixels to view pixels, for tests and tooltips. */
	QPointF toView(const QPointF &point) const;

signals:
	void selectionChanged(const QString &primary);

	/* Sent once, before the first change of a drag or nudge, so that the
	 * editor can switch the widget to free placement first. */
	void dragStarted(const QString &id);

	/* Sent while dragging, with `finished` set on release: every element
	 * the drag moved, in one go. */
	void elementsMoved(const QVector<vr::editor::CanvasMove> &moves, bool finished);

	/* The same, one element at a time, kept for listeners that only care
	 * about one. Sent after elementsMoved. */
	void elementMoved(const QString &id, const QRectF &rect, bool finished);

	void zoomChanged(double zoom);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

private:
	enum class Mode { None, Pending, Moving, Band, Pan };

	struct Drag {
		Mode mode = Mode::None;
		QString id; /* the element grabbed */
		unsigned edges = 0;
		QVector<CanvasMove> start; /* where each moving element began */
		QPointF start_mouse;       /* widget pixels */
		QPointF start_view;        /* view pixels */
		QPointF start_pan;
		bool additive = false; /* the band adds to the selection */
		bool reselect = false; /* a plain click on part of a selection */
	};

	double fitScale() const;
	double scale() const;
	QPointF origin() const;
	QPointF fromView(const QPointF &point) const;
	QRectF viewRect(const QRectF &rect) const;

	int indexOf(const QString &id) const;
	bool isSelected(const QString &id) const { return selection_.contains(id); }
	bool canMove(const QString &id) const;
	QString elementAt(const QPointF &view_point) const;
	unsigned handleAt(const QPointF &view_point) const;
	QVector<QRectF> handleRects(const QRectF &view_rect) const;
	void updateCursor(const QPointF &view_point);
	void applySelection(const QStringList &ids, const QString &primary);
	void emitMoves(const QVector<CanvasMove> &moves, bool finished);
	void zoomAround(double zoom, const QPointF &view_point);

	QSize canvas_size_{640, 180};
	QImage preview_;
	QVector<CanvasElement> elements_;
	QStringList selection_;
	QString primary_;
	QString hovered_;
	Drag drag_;
	QRectF band_; /* view pixels */
	std::vector<Guide> guides_;

	double zoom_ = 1.0;
	QPointF pan_; /* view pixels */

	bool grid_show_ = false;
	bool grid_snap_ = false;
	int grid_size_ = 10;
};

} // namespace vr::editor
