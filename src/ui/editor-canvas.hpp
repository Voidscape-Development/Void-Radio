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
#include <QVector>
#include <QWidget>

namespace vr::editor {

struct CanvasElement {
	QString id;
	QString label;
	QRectF rect; /* widget pixels, from the top left */
	bool visible = true;
};

/*
 * The middle of the layout editor: the widget as OBS draws it, with every
 * element outlined, and handles to move and resize the selected one.
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

	void setSelected(const QString &id);
	QString selected() const { return selected_; }

	/* Widget pixels to view pixels, for tests and tooltips. */
	QPointF toView(const QPointF &point) const;

signals:
	void selectionChanged(const QString &id);

	/* Sent once, before the first change of a drag or nudge, so that the
	 * editor can switch the widget to free placement first. */
	void dragStarted(const QString &id);

	/* Sent while dragging, with `finished` set on release. */
	void elementMoved(const QString &id, const QRectF &rect, bool finished);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	struct Drag {
		QString id;
		unsigned edges = 0;
		QRectF start_rect;
		QPointF start_mouse; /* widget pixels */
		bool active = false;
	};

	double scale() const;
	QPointF origin() const;
	QPointF fromView(const QPointF &point) const;
	QRectF viewRect(const QRectF &rect) const;

	int indexOf(const QString &id) const;
	QString elementAt(const QPointF &view_point) const;
	unsigned handleAt(const QPointF &view_point) const;
	QVector<QRectF> handleRects(const QRectF &view_rect) const;
	void updateCursor(const QPointF &view_point);

	QSize canvas_size_{640, 180};
	QImage preview_;
	QVector<CanvasElement> elements_;
	QString selected_;
	QString hovered_;
	Drag drag_;
	std::vector<Guide> guides_;
};

} // namespace vr::editor
