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

#include "ui/widget-editor.hpp"
#include "music/music-api.hpp"
#include "sources/music-widget/widget-design.hpp"
#include "ui/editor-canvas.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace vr {

namespace {

using widget::Arrangement;
using widget::Box;
using widget::WidgetStyle;

constexpr int MAX_UNDO = 50;
constexpr uint32_t MAX_PREVIEW = 2048;

QString tr_text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

bool is_widget(obs_source_t *source)
{
	const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
	return id && strcmp(id, MUSIC_WIDGET_SOURCE_ID) == 0;
}

/* Strong references to every Music Widget; the caller releases them. */
std::vector<obs_source_t *> all_widgets()
{
	std::vector<obs_source_t *> out;

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (is_widget(source)) {
				if (obs_source_t *ref = obs_source_get_ref(source))
					static_cast<std::vector<obs_source_t *> *>(param)->push_back(ref);
			}
			return true;
		},
		&out);

	return out;
}

/* A widget selected in the current scene, if there is one. */
obs_source_t *selected_widget()
{
	obs_source_t *scene_source = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	obs_source_t *found = nullptr;

	if (scene) {
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				obs_source_t *source = obs_sceneitem_get_source(item);
				if (obs_sceneitem_selected(item) && is_widget(source)) {
					*static_cast<obs_source_t **>(param) = obs_source_get_ref(source);
					return false;
				}
				return true;
			},
			&found);
	}

	obs_source_release(scene_source);
	return found;
}

struct Choice {
	const char *text;
	int value;
};

const Choice LAYOUTS[] = {
	{"Widget.Layout.ArtLeft", (int)Arrangement::ArtLeft}, {"Widget.Layout.ArtRight", (int)Arrangement::ArtRight},
	{"Widget.Layout.Stacked", (int)Arrangement::Stacked}, {"Widget.Layout.Backdrop", (int)Arrangement::Backdrop},
	{"Widget.Layout.Compact", (int)Arrangement::Compact}, {"Widget.Layout.TextOnly", (int)Arrangement::TextOnly},
	{"Widget.Layout.Free", (int)Arrangement::Free},
};

const Choice STYLES[] = {
	{"Widget.Style.Glass", (int)WidgetStyle::Glass},     {"Widget.Style.Solid", (int)WidgetStyle::Solid},
	{"Widget.Style.Minimal", (int)WidgetStyle::Minimal}, {"Widget.Style.Outline", (int)WidgetStyle::Outline},
	{"Widget.Style.Vinyl", (int)WidgetStyle::Vinyl},     {"Widget.Style.Custom", (int)WidgetStyle::Custom},
};

class WidgetEditor;
QPointer<WidgetEditor> editor_instance;

/* ------------------------------------------------------------------------- */
/* The window                                                                 */
/* ------------------------------------------------------------------------- */

class WidgetEditor : public QDialog {
public:
	explicit WidgetEditor(QWidget *parent);
	~WidgetEditor() override;

	void showWidget(obs_source_t *source);

private:
	/* UI thread. */
	obs_source_t *acquire() const;
	void populateWidgets();
	void refresh();
	void refreshInspector();
	void pushUndo(obs_data_t *settings);
	void undo();

	/* Runs `change` on the widget's settings and applies them. */
	template<typename Fn> void edit(Fn change, bool undoable);

	void onDragStarted();
	void onElementMoved(const QString &id, const QRectF &rect, bool finished);
	void onInspectorChanged();
	void onVisibilityToggled(QListWidgetItem *item);

	/* Graphics thread. */
	static void renderCallback(void *data, uint32_t cx, uint32_t cy);
	void renderPreview();
	void pullPreview();

	obs_weak_source_t *weak_ = nullptr;

	QComboBox *widgetCombo_ = nullptr;
	QComboBox *layoutCombo_ = nullptr;
	QComboBox *styleCombo_ = nullptr;
	QSpinBox *widthSpin_ = nullptr;
	QSpinBox *heightSpin_ = nullptr;
	QPushButton *undoButton_ = nullptr;
	QPushButton *propertiesButton_ = nullptr;

	editor::EditorCanvas *canvas_ = nullptr;
	QLabel *hint_ = nullptr;

	QListWidget *elementList_ = nullptr;
	QLabel *selectedLabel_ = nullptr;
	QSpinBox *xSpin_ = nullptr;
	QSpinBox *ySpin_ = nullptr;
	QSpinBox *wSpin_ = nullptr;
	QSpinBox *hSpin_ = nullptr;
	QLineEdit *lineText_ = nullptr;
	QLabel *lineTextLabel_ = nullptr;

	QTimer *previewTimer_ = nullptr;
	QTimer *syncTimer_ = nullptr;

	std::vector<widget::ElementBox> elements_;
	QStringList undo_;
	std::string lastJson_;
	bool updating_ = false;

	/* Shared with the graphics thread. */
	std::mutex captureMutex_;
	obs_weak_source_t *captureSource_ = nullptr;
	std::vector<uint8_t> pixels_;
	uint32_t pixelWidth_ = 0;
	uint32_t pixelHeight_ = 0;
	bool freshPixels_ = false;

	/* Graphics thread only. */
	gs_texrender_t *texrender_ = nullptr;
	gs_stagesurf_t *stage_ = nullptr;
	uint32_t stageWidth_ = 0;
	uint32_t stageHeight_ = 0;
};

WidgetEditor::WidgetEditor(QWidget *parent) : QDialog(parent, Qt::Window)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr_text("Editor.Title"));
	resize(1100, 680);

	QVBoxLayout *root = new QVBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(8);

	/* Top bar: which widget, and the decisions that shape the whole card. */
	QHBoxLayout *bar = new QHBoxLayout();
	bar->setSpacing(8);

	widgetCombo_ = new QComboBox(this);
	widgetCombo_->setMinimumWidth(180);
	layoutCombo_ = new QComboBox(this);
	for (const Choice &choice : LAYOUTS)
		layoutCombo_->addItem(tr_text(choice.text), choice.value);
	styleCombo_ = new QComboBox(this);
	for (const Choice &choice : STYLES)
		styleCombo_->addItem(tr_text(choice.text), choice.value);

	widthSpin_ = new QSpinBox(this);
	widthSpin_->setRange(16, 8192);
	widthSpin_->setSuffix(QStringLiteral(" px"));
	heightSpin_ = new QSpinBox(this);
	heightSpin_->setRange(16, 8192);
	heightSpin_->setSuffix(QStringLiteral(" px"));

	undoButton_ = new QPushButton(tr_text("Editor.Undo"), this);
	undoButton_->setToolTip(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText));
	propertiesButton_ = new QPushButton(tr_text("Editor.Properties"), this);

	bar->addWidget(new QLabel(tr_text("Editor.Widget"), this));
	bar->addWidget(widgetCombo_);
	bar->addSpacing(8);
	bar->addWidget(new QLabel(tr_text("Widget.Layout"), this));
	bar->addWidget(layoutCombo_);
	bar->addWidget(new QLabel(tr_text("Widget.Style"), this));
	bar->addWidget(styleCombo_);
	bar->addWidget(widthSpin_);
	bar->addWidget(new QLabel(QStringLiteral("×"), this));
	bar->addWidget(heightSpin_);
	bar->addStretch(1);
	bar->addWidget(undoButton_);
	bar->addWidget(propertiesButton_);
	root->addLayout(bar);

	/* The canvas, and beside it the element list and the inspector. */
	QSplitter *split = new QSplitter(Qt::Horizontal, this);

	canvas_ = new editor::EditorCanvas(split);
	split->addWidget(canvas_);

	QWidget *side = new QWidget(split);
	QVBoxLayout *sideLayout = new QVBoxLayout(side);
	sideLayout->setContentsMargins(8, 0, 0, 0);

	elementList_ = new QListWidget(side);
	elementList_->setMinimumWidth(220);
	sideLayout->addWidget(elementList_, 1);

	selectedLabel_ = new QLabel(side);
	QFont bold = selectedLabel_->font();
	bold.setBold(true);
	selectedLabel_->setFont(bold);
	sideLayout->addWidget(selectedLabel_);

	QFormLayout *form = new QFormLayout();
	const auto spin = [side]() {
		QSpinBox *box = new QSpinBox(side);
		box->setRange(-8192, 8192);
		box->setSuffix(QStringLiteral(" px"));
		box->setKeyboardTracking(false);
		return box;
	};
	xSpin_ = spin();
	ySpin_ = spin();
	wSpin_ = spin();
	hSpin_ = spin();
	wSpin_->setMinimum(1);
	hSpin_->setMinimum(1);
	form->addRow(tr_text("Editor.X"), xSpin_);
	form->addRow(tr_text("Editor.Y"), ySpin_);
	form->addRow(tr_text("Editor.Width"), wSpin_);
	form->addRow(tr_text("Editor.Height"), hSpin_);

	lineText_ = new QLineEdit(side);
	lineText_->setToolTip(tr_text("Info.Fields"));
	lineTextLabel_ = new QLabel(tr_text("Editor.Line"), side);
	form->addRow(lineTextLabel_, lineText_);
	sideLayout->addLayout(form);

	split->addWidget(side);
	split->setStretchFactor(0, 1);
	split->setStretchFactor(1, 0);
	split->setSizes({820, 260});
	root->addWidget(split, 1);

	hint_ = new QLabel(this);
	hint_->setWordWrap(true);
	root->addWidget(hint_);

	/* Wiring. */
	connect(widgetCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		const QByteArray uuid = widgetCombo_->itemData(index).toString().toUtf8();
		obs_source_t *source = obs_get_source_by_uuid(uuid.constData());
		if (source) {
			showWidget(source);
			obs_source_release(source);
		}
	});

	connect(layoutCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		const int value = layoutCombo_->itemData(index).toInt();
		edit([value](obs_data_t *settings) { obs_data_set_int(settings, "layout", value); }, true);
	});

	connect(styleCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		const int value = styleCombo_->itemData(index).toInt();
		edit([value](obs_data_t *settings) { obs_data_set_int(settings, "style", value); }, true);
	});

	const auto size_changed = [this]() {
		if (updating_)
			return;
		const int w = widthSpin_->value();
		const int h = heightSpin_->value();
		edit(
			[w, h](obs_data_t *settings) {
				obs_data_set_int(settings, "width", w);
				obs_data_set_int(settings, "height", h);
			},
			true);
	};
	widthSpin_->setKeyboardTracking(false);
	heightSpin_->setKeyboardTracking(false);
	connect(widthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, size_changed);
	connect(heightSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, size_changed);

	connect(undoButton_, &QPushButton::clicked, this, &WidgetEditor::undo);
	QShortcut *undoShortcut = new QShortcut(QKeySequence::Undo, this);
	connect(undoShortcut, &QShortcut::activated, this, &WidgetEditor::undo);

	connect(propertiesButton_, &QPushButton::clicked, this, [this]() {
		if (obs_source_t *source = acquire()) {
			obs_frontend_open_source_properties(source);
			obs_source_release(source);
		}
	});

	connect(canvas_, &editor::EditorCanvas::selectionChanged, this, [this](const QString &id) {
		updating_ = true;
		for (int i = 0; i < elementList_->count(); i++) {
			QListWidgetItem *item = elementList_->item(i);
			item->setSelected(item->data(Qt::UserRole).toString() == id);
		}
		updating_ = false;
		refreshInspector();
	});
	connect(canvas_, &editor::EditorCanvas::dragStarted, this, [this](const QString &) { onDragStarted(); });
	connect(canvas_, &editor::EditorCanvas::elementMoved, this, &WidgetEditor::onElementMoved);

	connect(elementList_, &QListWidget::itemSelectionChanged, this, [this]() {
		if (updating_)
			return;
		const QList<QListWidgetItem *> items = elementList_->selectedItems();
		canvas_->setSelected(items.isEmpty() ? QString() : items.first()->data(Qt::UserRole).toString());
	});
	connect(elementList_, &QListWidget::itemChanged, this, &WidgetEditor::onVisibilityToggled);

	for (QSpinBox *box : {xSpin_, ySpin_, wSpin_, hSpin_})
		connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this, &WidgetEditor::onInspectorChanged);

	connect(lineText_, &QLineEdit::editingFinished, this, [this]() {
		const std::string prefix = canvas_->selected().toStdString();
		if (prefix.size() != 3 || prefix[0] != 't')
			return;
		const std::string text = lineText_->text().toStdString();
		edit(
			[prefix, text](obs_data_t *settings) {
				obs_data_set_string(settings, (prefix + "format").c_str(), text.c_str());
			},
			true);
	});

	/* The live preview, and picking up changes made elsewhere, such as in
	 * the properties window. */
	previewTimer_ = new QTimer(this);
	previewTimer_->setInterval(33);
	connect(previewTimer_, &QTimer::timeout, this, &WidgetEditor::pullPreview);
	previewTimer_->start();

	syncTimer_ = new QTimer(this);
	syncTimer_->setInterval(400);
	connect(syncTimer_, &QTimer::timeout, this, &WidgetEditor::refresh);
	syncTimer_->start();

	obs_add_main_render_callback(renderCallback, this);
}

WidgetEditor::~WidgetEditor()
{
	/* After this returns the graphics thread no longer calls in. */
	obs_remove_main_render_callback(renderCallback, this);

	obs_enter_graphics();
	gs_texrender_destroy(texrender_);
	gs_stagesurface_destroy(stage_);
	obs_leave_graphics();

	{
		std::lock_guard<std::mutex> lock(captureMutex_);
		obs_weak_source_release(captureSource_);
		captureSource_ = nullptr;
	}

	obs_weak_source_release(weak_);
}

obs_source_t *WidgetEditor::acquire() const
{
	return weak_ ? obs_weak_source_get_source(weak_) : nullptr;
}

void WidgetEditor::showWidget(obs_source_t *source)
{
	obs_weak_source_release(weak_);
	weak_ = source ? obs_source_get_weak_source(source) : nullptr;

	{
		std::lock_guard<std::mutex> lock(captureMutex_);
		obs_weak_source_release(captureSource_);
		captureSource_ = source ? obs_source_get_weak_source(source) : nullptr;
		freshPixels_ = false;
	}

	undo_.clear();
	lastJson_.clear();
	canvas_->setPreview(QImage());
	canvas_->setSelected(QString());

	populateWidgets();
	refresh();
}

void WidgetEditor::populateWidgets()
{
	obs_source_t *current = acquire();

	widgetCombo_->clear();
	for (obs_source_t *widget : all_widgets()) {
		widgetCombo_->addItem(QString::fromUtf8(obs_source_get_name(widget)),
				      QString::fromUtf8(obs_source_get_uuid(widget)));
		if (widget == current)
			widgetCombo_->setCurrentIndex(widgetCombo_->count() - 1);
		obs_source_release(widget);
	}

	obs_source_release(current);
}

/* ------------------------------------------------------------------------- */
/* Settings                                                                   */
/* ------------------------------------------------------------------------- */

template<typename Fn> void WidgetEditor::edit(Fn change, bool undoable)
{
	obs_source_t *source = acquire();
	if (!source)
		return;

	obs_data_t *settings = obs_source_get_settings(source);
	if (undoable)
		pushUndo(settings);

	change(settings);
	obs_source_update(source, settings);

	obs_data_release(settings);
	obs_source_release(source);

	refresh();
}

void WidgetEditor::pushUndo(obs_data_t *settings)
{
	undo_.append(QString::fromUtf8(obs_data_get_json(settings)));
	while (undo_.size() > MAX_UNDO)
		undo_.removeFirst();
	undoButton_->setEnabled(true);
}

void WidgetEditor::undo()
{
	if (undo_.isEmpty())
		return;

	obs_source_t *source = acquire();
	if (!source)
		return;

	obs_data_t *previous = obs_data_create_from_json(undo_.takeLast().toUtf8().constData());
	if (previous) {
		obs_source_reset_settings(source, previous);
		obs_data_release(previous);
	}

	obs_source_release(source);
	refresh();
}

void WidgetEditor::refresh()
{
	obs_source_t *source = acquire();

	if (!source) {
		/* The widget went away; fall back to another, or explain. */
		obs_source_t *other = selected_widget();
		if (!other) {
			std::vector<obs_source_t *> widgets = all_widgets();
			if (!widgets.empty())
				other = obs_source_get_ref(widgets.front());
			for (obs_source_t *w : widgets)
				obs_source_release(w);
		}

		if (other) {
			showWidget(other);
			obs_source_release(other);
			return;
		}

		hint_->setText(tr_text("Editor.NoWidgets"));
		canvas_->setElements({});
		canvas_->setPreview(QImage());
		for (QWidget *w : std::initializer_list<QWidget *>{layoutCombo_, styleCombo_, widthSpin_, heightSpin_,
								   propertiesButton_, elementList_})
			w->setEnabled(false);
		widgetCombo_->clear();
		return;
	}

	for (QWidget *w : std::initializer_list<QWidget *>{layoutCombo_, styleCombo_, widthSpin_, heightSpin_,
							   propertiesButton_, elementList_})
		w->setEnabled(true);

	obs_data_t *settings = obs_source_get_settings(source);
	const std::string json = obs_data_get_json(settings);

	if (json == lastJson_) {
		obs_data_release(settings);
		obs_source_release(source);
		return;
	}
	lastJson_ = json;

	updating_ = true;

	const Arrangement layout = (Arrangement)obs_data_get_int(settings, "layout");
	layoutCombo_->setCurrentIndex(layoutCombo_->findData((int)layout));
	styleCombo_->setCurrentIndex(styleCombo_->findData((int)obs_data_get_int(settings, "style")));
	widthSpin_->setValue((int)obs_data_get_int(settings, "width"));
	heightSpin_->setValue((int)obs_data_get_int(settings, "height"));
	canvas_->setCanvasSize(QSize(widthSpin_->value(), heightSpin_->value()));

	elements_ = widget::element_boxes(settings);

	QVector<editor::CanvasElement> canvasElements;
	for (const widget::ElementBox &element : elements_) {
		editor::CanvasElement item;
		item.id = QString::fromStdString(element.prefix);
		item.label = QString::fromStdString(element.label);
		item.rect = QRectF(element.box.x, element.box.y, element.box.width, element.box.height);
		item.visible = element.enabled;
		canvasElements.append(item);
	}
	canvas_->setElements(canvasElements);

	/* The list mirrors the canvas, with a tick box for showing each one. */
	const QString selected = canvas_->selected();
	elementList_->clear();
	for (const widget::ElementBox &element : elements_) {
		QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(element.label), elementList_);
		item->setData(Qt::UserRole, QString::fromStdString(element.prefix));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(element.enabled ? Qt::Checked : Qt::Unchecked);
		item->setSelected(item->data(Qt::UserRole).toString() == selected);
	}

	undoButton_->setEnabled(!undo_.isEmpty());
	hint_->setText(tr_text(layout == Arrangement::Free ? "Editor.Hint.Free" : "Editor.Hint.Auto"));

	updating_ = false;

	obs_data_release(settings);
	obs_source_release(source);

	refreshInspector();
}

void WidgetEditor::refreshInspector()
{
	const std::string id = canvas_->selected().toStdString();
	const widget::ElementBox *element = nullptr;
	for (const widget::ElementBox &candidate : elements_)
		if (candidate.prefix == id)
			element = &candidate;

	updating_ = true;

	const bool have = element != nullptr;
	for (QSpinBox *box : {xSpin_, ySpin_, wSpin_, hSpin_})
		box->setEnabled(have && element->enabled);

	selectedLabel_->setText(have ? QString::fromStdString(element->label) : tr_text("Editor.Nothing"));

	if (have) {
		xSpin_->setValue((int)std::lround(element->box.x));
		ySpin_->setValue((int)std::lround(element->box.y));
		wSpin_->setValue((int)std::lround(element->box.width));
		hSpin_->setValue((int)std::lround(element->box.height));
	}

	const bool line = have && id.size() == 3 && id[0] == 't';
	lineText_->setVisible(line);
	lineTextLabel_->setVisible(line);

	if (line) {
		obs_source_t *source = acquire();
		obs_data_t *settings = source ? obs_source_get_settings(source) : nullptr;
		if (settings && !lineText_->hasFocus())
			lineText_->setText(QString::fromUtf8(obs_data_get_string(settings, (id + "format").c_str())));
		obs_data_release(settings);
		obs_source_release(source);
	}

	updating_ = false;
}

/* ------------------------------------------------------------------------- */
/* Editing                                                                    */
/* ------------------------------------------------------------------------- */

/* The first change of a drag. An automatic layout becomes free placement here,
 * starting from where everything already is, so nothing jumps. */
void WidgetEditor::onDragStarted()
{
	edit(
		[](obs_data_t *settings) {
			if ((Arrangement)obs_data_get_int(settings, "layout") != Arrangement::Free)
				widget::detach_to_free_placement(settings);
		},
		true);
}

void WidgetEditor::onElementMoved(const QString &id, const QRectF &rect, bool finished)
{
	UNUSED_PARAMETER(finished);

	Box box;
	box.x = (float)rect.x();
	box.y = (float)rect.y();
	box.width = (float)rect.width();
	box.height = (float)rect.height();
	box.visible = true;

	const std::string prefix = id.toStdString();
	edit([prefix, box](obs_data_t *settings) { widget::place_element(settings, prefix, box); }, false);
}

void WidgetEditor::onInspectorChanged()
{
	if (updating_)
		return;

	const QString id = canvas_->selected();
	if (id.isEmpty())
		return;

	onDragStarted();

	QRectF rect(xSpin_->value(), ySpin_->value(), wSpin_->value(), hSpin_->value());
	onElementMoved(id, rect, true);
}

void WidgetEditor::onVisibilityToggled(QListWidgetItem *item)
{
	if (updating_ || !item)
		return;

	const std::string prefix = item->data(Qt::UserRole).toString().toStdString();
	const bool show = item->checkState() == Qt::Checked;

	edit(
		[prefix, show](obs_data_t *settings) {
			const Arrangement layout = (Arrangement)obs_data_get_int(settings, "layout");
			const bool line = prefix.size() == 3 && prefix[0] == 't';

			/* Lines are switched on and off the same way in every
			 * layout. Under an automatic layout the art and the bar
			 * are the layout's to place, so hiding them picks the
			 * layout or bar setting that leaves them out. */
			if (line || layout == Arrangement::Free) {
				obs_data_set_bool(settings, (prefix + "enable").c_str(), show);
			} else if (prefix == "bar_") {
				obs_data_set_int(settings, "bar_position",
						 (int64_t)(show ? widget::BarPlacement::UnderText
								: widget::BarPlacement::Off));
			} else if (prefix == "art_") {
				obs_data_set_int(settings, "layout",
						 (int64_t)(show ? Arrangement::ArtLeft : Arrangement::TextOnly));
			}
		},
		true);
}

/* ------------------------------------------------------------------------- */
/* Preview                                                                    */
/* ------------------------------------------------------------------------- */

void WidgetEditor::renderCallback(void *data, uint32_t, uint32_t)
{
	static_cast<WidgetEditor *>(data)->renderPreview();
}

/* Runs on the graphics thread once per frame: draws the widget into a texture
 * of its own and copies it back for the canvas. */
void WidgetEditor::renderPreview()
{
	obs_source_t *source = nullptr;
	{
		std::lock_guard<std::mutex> lock(captureMutex_);
		source = captureSource_ ? obs_weak_source_get_source(captureSource_) : nullptr;
	}
	if (!source)
		return;

	const uint32_t width = std::min(obs_source_get_width(source), MAX_PREVIEW);
	const uint32_t height = std::min(obs_source_get_height(source), MAX_PREVIEW);

	if (!width || !height) {
		obs_source_release(source);
		return;
	}

	if (!texrender_)
		texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

	if (!stage_ || stageWidth_ != width || stageHeight_ != height) {
		gs_stagesurface_destroy(stage_);
		stage_ = gs_stagesurface_create(width, height, GS_RGBA);
		stageWidth_ = width;
		stageHeight_ = height;
	}

	gs_texrender_reset(texrender_);

	if (gs_texrender_begin(texrender_, width, height)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		obs_source_video_render(source);
		gs_blend_state_pop();

		gs_texrender_end(texrender_);
		gs_stage_texture(stage_, gs_texrender_get_texture(texrender_));

		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(stage_, &data, &linesize)) {
			std::lock_guard<std::mutex> lock(captureMutex_);
			pixels_.resize((size_t)width * height * 4);
			for (uint32_t y = 0; y < height; y++)
				memcpy(&pixels_[(size_t)y * width * 4], data + (size_t)y * linesize, (size_t)width * 4);
			pixelWidth_ = width;
			pixelHeight_ = height;
			freshPixels_ = true;
			gs_stagesurface_unmap(stage_);
		}
	}

	obs_source_release(source);
}

void WidgetEditor::pullPreview()
{
	QImage image;

	{
		std::lock_guard<std::mutex> lock(captureMutex_);
		if (!freshPixels_)
			return;
		freshPixels_ = false;
		image = QImage(pixels_.data(), (int)pixelWidth_, (int)pixelHeight_, QImage::Format_RGBA8888).copy();
	}

	canvas_->setPreview(image);
}

} // namespace

/* ------------------------------------------------------------------------- */
/* Entry points                                                               */
/* ------------------------------------------------------------------------- */

void open_widget_editor(obs_source_t *widget)
{
	obs_source_t *target = widget ? obs_source_get_ref(widget) : selected_widget();

	if (!target) {
		std::vector<obs_source_t *> widgets = all_widgets();
		if (!widgets.empty())
			target = obs_source_get_ref(widgets.front());
		for (obs_source_t *w : widgets)
			obs_source_release(w);
	}

	if (!editor_instance) {
		QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		editor_instance = new WidgetEditor(main);
	}

	editor_instance->showWidget(target);
	editor_instance->show();
	editor_instance->raise();
	editor_instance->activateWindow();

	obs_source_release(target);
}

void register_widget_editor()
{
	obs_frontend_add_tools_menu_item(
		obs_module_text("Editor.Menu"), [](void *) { open_widget_editor(nullptr); }, nullptr);
}

void close_widget_editor()
{
	if (editor_instance)
		delete editor_instance.data();
}

} // namespace vr
