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
#include "sources/music-widget-source.hpp"
#include "sources/music-widget/widget-design.hpp"
#include "sources/music-widget/widget.hpp"
#include "ui/editor-canvas.hpp"
#include "util/text-template.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vr {

namespace {

using editor::Align;
using editor::CanvasMove;
using widget::Arrangement;
using widget::Box;
using widget::WidgetStyle;

constexpr int MAX_UNDO = 100;
constexpr int COALESCE_MS = 1200;
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

bool is_line(const std::string &prefix)
{
	return prefix.size() == 3 && prefix[0] == 't';
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
	{"Widget.Style.Glass", (int)WidgetStyle::Glass},       {"Widget.Style.Solid", (int)WidgetStyle::Solid},
	{"Widget.Style.Minimal", (int)WidgetStyle::Minimal},   {"Widget.Style.Outline", (int)WidgetStyle::Outline},
	{"Widget.Style.Vinyl", (int)WidgetStyle::Vinyl},       {"Widget.Style.Neon", (int)WidgetStyle::Neon},
	{"Widget.Style.Cassette", (int)WidgetStyle::Cassette}, {"Widget.Style.Ticker", (int)WidgetStyle::Ticker},
	{"Widget.Style.Custom", (int)WidgetStyle::Custom},
};

/* ------------------------------------------------------------------------- */
/* Inspector fields                                                           */
/* ------------------------------------------------------------------------- */

/*
 * What the inspector offers for each kind of element, as a table rather than
 * hand-built forms. Each field is a setting under the element's prefix. A
 * "styled" field is one the named style decides; changing it switches the
 * element to keeping its own look, starting from what the style gave it.
 */

enum class FieldKind { Int, Double, Percent, Bool, Color, Combo, Font, Template, Path };

struct Field {
	const char *suffix;
	const char *label;
	FieldKind kind;
	double min = 0.0;
	double max = 0.0;
	double step = 1.0;
	const char *unit = nullptr;
	std::vector<Choice> options = {};
	bool styled = false;
	const char *shown_if = nullptr; /* a bool under the same prefix */
};

const std::vector<Choice> TONES = {
	{"Widget.Tone.Accent", (int)widget::AccentTone::Accent},
	{"Widget.Tone.Dark", (int)widget::AccentTone::Dark},
	{"Widget.Tone.Light", (int)widget::AccentTone::Light},
	{"Widget.Tone.Contrast", (int)widget::AccentTone::Contrast},
};

const std::vector<Choice> FITS = {
	{"Widget.Art.Fit.Cover", (int)widget::ArtFit::Cover},
	{"Widget.Art.Fit.Contain", (int)widget::ArtFit::Contain},
	{"Widget.Art.Fit.Stretch", (int)widget::ArtFit::Stretch},
};

const std::vector<Field> &fields_for(const std::string &prefix)
{
	static const std::vector<Field> TEXT = {
		{"format", "Editor.Line", FieldKind::Template},
		{"idle_format", "Info.IdleFormat", FieldKind::Template},
		{"font", "Widget.Text.Font", FieldKind::Font, 0, 0, 1, nullptr, {}, true},
		{"color", "Bar.Color", FieldKind::Color, 0, 0, 1, nullptr, {}, true},
		{"accent", "Bar.FollowAccent", FieldKind::Bool, 0, 0, 1, nullptr, {}, true},
		{"tone", "Widget.Tone", FieldKind::Combo, 0, 0, 1, nullptr, TONES, true, "accent"},
		{"align",
		 "Widget.Text.Align",
		 FieldKind::Combo,
		 0,
		 0,
		 1,
		 nullptr,
		 {{"Widget.Text.Align.Left", (int)TextAlign::Left},
		  {"Widget.Text.Align.Center", (int)TextAlign::Center},
		  {"Widget.Text.Align.Right", (int)TextAlign::Right}},
		 true},
		{"overflow",
		 "Widget.Text.Overflow",
		 FieldKind::Combo,
		 0,
		 0,
		 1,
		 nullptr,
		 {{"Widget.Text.Overflow.None", (int)TextOverflow::None},
		  {"Widget.Text.Overflow.Marquee", (int)TextOverflow::Marquee},
		  {"Widget.Text.Overflow.Shrink", (int)TextOverflow::Shrink},
		  {"Widget.Text.Overflow.Ellipsis", (int)TextOverflow::Ellipsis},
		  {"Widget.Text.Overflow.Wrap", (int)TextOverflow::Wrap}},
		 true},
		{"shadow", "Widget.Shadow", FieldKind::Bool, 0, 0, 1, nullptr, {}, true},
		{"opacity", "Bar.Opacity", FieldKind::Percent, 0, 100, 1, "%"},
	};

	static const std::vector<Field> ART = {
		{"corner_radius", "Bar.CornerRadius", FieldKind::Double, 0, 512, 1, " px", {}, true},
		{"border_width", "Bar.BorderWidth", FieldKind::Double, 0, 64, 1, " px", {}, true},
		{"border_color", "Bar.BorderColor", FieldKind::Color, 0, 0, 1, nullptr, {}, true},
		{"shadow", "Widget.Shadow", FieldKind::Bool, 0, 0, 1, nullptr, {}, true},
		{"animation",
		 "Widget.Art.Animation",
		 FieldKind::Combo,
		 0,
		 0,
		 1,
		 nullptr,
		 {{"Widget.Art.Animation.None", (int)widget::ArtAnimation::None},
		  {"Widget.Art.Animation.Spin", (int)widget::ArtAnimation::Spin},
		  {"Widget.Art.Animation.Pulse", (int)widget::ArtAnimation::Pulse}}},
		{"opacity", "Bar.Opacity", FieldKind::Percent, 0, 100, 1, "%"},
	};

	static const std::vector<Field> BAR = {
		{"fg_color", "Editor.BarFill", FieldKind::Color, 0, 0, 1, nullptr, {}, true},
		{"fg_accent", "Bar.FollowAccent", FieldKind::Bool, 0, 0, 1, nullptr, {}, true},
		{"bg_color", "Editor.BarTrack", FieldKind::Color, 0, 0, 1, nullptr, {}, true},
		{"corner_radius", "Bar.CornerRadius", FieldKind::Double, 0, 256, 1, " px", {}, true},
	};

	static const std::vector<Field> PANEL = {
		{"type",
		 "Widget.Background.Type",
		 FieldKind::Combo,
		 0,
		 0,
		 1,
		 nullptr,
		 {{"Bar.FillType.Color", (int)widget::BackgroundType::Color},
		  {"Bar.FillType.Gradient", (int)widget::BackgroundType::Gradient},
		  {"Bar.FillType.Image", (int)widget::BackgroundType::Image},
		  {"Widget.Background.BlurredArt", (int)widget::BackgroundType::BlurredArt}},
		 true},
		{"color", "Bar.Color", FieldKind::Color, 0, 0, 1, nullptr, {}, true},
		{"accent", "Bar.FollowAccent", FieldKind::Bool, 0, 0, 1, nullptr, {}, true},
		{"tone", "Widget.Tone", FieldKind::Combo, 0, 0, 1, nullptr, TONES, true, "accent"},
		{"color2", "Bar.Color2", FieldKind::Color, 0, 0, 1, nullptr, {}, true},
		{"corner_radius", "Bar.CornerRadius", FieldKind::Double, 0, 512, 1, " px", {}, true},
		{"border_width", "Bar.BorderWidth", FieldKind::Double, 0, 64, 1, " px", {}, true},
		{"border_color", "Bar.BorderColor", FieldKind::Color, 0, 0, 1, nullptr, {}, true},
		{"border_accent", "Bar.BorderFollowAccent", FieldKind::Bool, 0, 0, 1, nullptr, {}, true},
		{"opacity", "Bar.Opacity", FieldKind::Percent, 0, 100, 1, "%", {}, true},
	};

	static const std::vector<Field> VIZ = {
		{"mode",
		 "Widget.Viz.Mode",
		 FieldKind::Combo,
		 0,
		 0,
		 1,
		 nullptr,
		 {{"Widget.Viz.Mode.Bars", (int)widget::VizMode::Bars},
		  {"Widget.Viz.Mode.Mirror", (int)widget::VizMode::Mirror},
		  {"Widget.Viz.Mode.Wave", (int)widget::VizMode::Wave},
		  {"Widget.Viz.Mode.Ring", (int)widget::VizMode::Ring}}},
		{"bands", "Widget.Viz.Bands", FieldKind::Int, 4, 128, 1},
		{"gap", "Widget.Viz.Gap", FieldKind::Double, 0, 90, 1, "%"},
		{"rounded", "Widget.Viz.Rounded", FieldKind::Bool},
		{"color", "Bar.Color", FieldKind::Color},
		{"accent", "Bar.FollowAccent", FieldKind::Bool},
		{"tone", "Widget.Tone", FieldKind::Combo, 0, 0, 1, nullptr, TONES, false, "accent"},
		{"gradient", "Widget.Viz.Gradient", FieldKind::Bool},
		{"color2", "Widget.Viz.TopColor", FieldKind::Color, 0, 0, 1, nullptr, {}, false, "gradient"},
		{"sensitivity", "Widget.Viz.Sensitivity", FieldKind::Double, 10, 400, 5, "%"},
		{"smoothing", "Widget.Viz.Smoothing", FieldKind::Double, 0, 95, 1, "%"},
		{"opacity", "Bar.Opacity", FieldKind::Percent, 0, 100, 1, "%"},
	};

	static const std::vector<Field> IMAGE = {
		{"path", "Bar.Image", FieldKind::Path},
		{"fit", "Widget.Art.Fit", FieldKind::Combo, 0, 0, 1, nullptr, FITS},
		{"corner_radius", "Bar.CornerRadius", FieldKind::Double, 0, 512, 1, " px"},
		{"opacity", "Bar.Opacity", FieldKind::Percent, 0, 100, 1, "%"},
	};

	static const std::vector<Field> ICON = {
		{"show",
		 "Widget.Icon.Show",
		 FieldKind::Combo,
		 0,
		 0,
		 1,
		 nullptr,
		 {{"Widget.Icon.Show.Always", (int)widget::IconShow::Always},
		  {"Widget.Icon.Show.NotPlaying", (int)widget::IconShow::NotPlaying}}},
		{"color", "Bar.Color", FieldKind::Color},
		{"accent", "Bar.FollowAccent", FieldKind::Bool},
		{"tone", "Widget.Tone", FieldKind::Combo, 0, 0, 1, nullptr, TONES, false, "accent"},
		{"opacity", "Bar.Opacity", FieldKind::Percent, 0, 100, 1, "%"},
	};

	static const std::vector<Field> GLOW = {
		{"softness", "Widget.Glow.Softness", FieldKind::Double, 0, 128, 1, " px"},
		{"corner_radius", "Bar.CornerRadius", FieldKind::Double, 0, 512, 1, " px"},
		{"opacity", "Bar.Opacity", FieldKind::Percent, 0, 100, 1, "%"},
	};

	static const std::vector<Field> NONE;

	if (is_line(prefix))
		return TEXT;
	if (prefix == "art_")
		return ART;
	if (prefix == "bar_")
		return BAR;
	if (prefix == "bg_")
		return PANEL;
	if (prefix == "viz_")
		return VIZ;
	if (prefix == "img_")
		return IMAGE;
	if (prefix == "icon_")
		return ICON;
	if (prefix == "glow_")
		return GLOW;
	return NONE;
}

/* Elements a named style dresses, which can opt out with "<prefix>custom". */
bool has_own_look(const std::string &prefix)
{
	return is_line(prefix) || prefix == "art_" || prefix == "bar_" || prefix == "bg_";
}

/* OBS colours are 0xAABBGGRR. */
QColor to_qcolor(uint32_t c)
{
	return QColor(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, (c >> 24) & 0xFF);
}

uint32_t from_qcolor(const QColor &c)
{
	return ((uint32_t)c.alpha() << 24) | ((uint32_t)c.blue() << 16) | ((uint32_t)c.green() << 8) |
	       (uint32_t)c.red();
}

QIcon swatch_icon(uint32_t color)
{
	QPixmap pixmap(28, 14);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.fillRect(QRect(0, 0, 14, 7), QColor(200, 200, 200));
	p.fillRect(QRect(14, 7, 14, 7), QColor(200, 200, 200));
	p.fillRect(QRect(14, 0, 14, 7), QColor(255, 255, 255));
	p.fillRect(QRect(0, 7, 14, 7), QColor(255, 255, 255));
	p.fillRect(pixmap.rect(), to_qcolor(color));
	p.setPen(QColor(0, 0, 0, 90));
	p.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
	return QIcon(pixmap);
}

/* Small drawn icons for the align buttons, so nothing depends on an icon
 * theme being installed. */
QIcon align_icon(Align how)
{
	QPixmap pixmap(18, 18);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	const QColor ink(220, 220, 225);
	p.setPen(QPen(ink, 1.5));

	const bool vertical_line = how == Align::Left || how == Align::HorizontalCenter || how == Align::Right;
	const double line = how == Align::Left || how == Align::Top       ? 2.5
			    : how == Align::Right || how == Align::Bottom ? 15.5
									  : 9.0;
	if (vertical_line)
		p.drawLine(QPointF(line, 1), QPointF(line, 17));
	else
		p.drawLine(QPointF(1, line), QPointF(17, line));

	p.setPen(Qt::NoPen);
	p.setBrush(ink);

	const auto bar = [&](double along, double length, double across) {
		/* `along` is where the bar sits on the axis the line crosses. */
		double start = how == Align::Left || how == Align::Top       ? line
			       : how == Align::Right || how == Align::Bottom ? line - length
									     : line - length / 2;
		if (vertical_line)
			p.drawRect(QRectF(start, along, length, across));
		else
			p.drawRect(QRectF(along, start, across, length));
	};
	bar(3.5, 11, 4);
	bar(10.5, 7, 4);
	return QIcon(pixmap);
}

QIcon distribute_icon(bool horizontal)
{
	QPixmap pixmap(18, 18);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(220, 220, 225));
	for (int i = 0; i < 3; i++) {
		const double at = 1.5 + i * 6.0;
		if (horizontal)
			p.drawRect(QRectF(at, 4, 3, 10));
		else
			p.drawRect(QRectF(4, at, 10, 3));
	}
	return QIcon(pixmap);
}

QString presets_dir()
{
	char *path = obs_module_config_path("presets");
	QString dir = QString::fromUtf8(path ? path : "");
	bfree(path);
	if (!dir.isEmpty())
		QDir().mkpath(dir);
	return dir;
}

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
	/* A field in the inspector, and how to put the setting into it. */
	struct Bound {
		Field field;
		QWidget *row = nullptr;
		QLabel *label = nullptr;
		std::function<void(obs_data_t *resolved)> load;
	};

	/* UI thread. */
	obs_source_t *acquire() const;
	void populateWidgets();
	void refresh(bool force = false);
	void refreshLayers();
	void refreshInspector(bool rebuild);
	void buildInspector();
	QString focusId() const { return focus_; }
	void setFocusId(const QString &id);

	void pushUndo(obs_data_t *settings, const QString &key);
	void undo();
	void redo();
	void restore(const QString &json);

	/* Runs `change` on the widget's settings and applies them. Changes with
	 * the same non-empty key, close together, make one undo step. */
	template<typename Fn> void edit(Fn change, const QString &undo_key = QString(), bool undoable = true);

	/* Sets one inspector field, first taking over the style's look for the
	 * element when the field is one the style decides. */
	void setField(const std::string &prefix, const Field &field, const std::function<void(obs_data_t *)> &write,
		      bool coalesce);

	void onDragStarted();
	void onElementsMoved(const QVector<CanvasMove> &moves, bool finished);
	void onInspectorGeometry();
	void onLayerChanged(QTreeWidgetItem *item, int column);
	void onLayerSelection();
	void restack(int direction); /* +1 forward, -1 back, +2 front, -2 back */
	void alignSelection(Align how);
	void distributeSelection(bool horizontal);
	void applyMoves(const QVector<CanvasMove> &moves, bool undoable);
	QVector<CanvasMove> movableSelection() const;

	void setSampleTrack(bool on);
	void showPresetsMenu();
	void applyDesign(const QString &json, const QString &what);
	void copyDesignTo(obs_source_t *target);

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
	QToolButton *presetsButton_ = nullptr;
	QPushButton *undoButton_ = nullptr;
	QPushButton *redoButton_ = nullptr;
	QPushButton *propertiesButton_ = nullptr;

	QCheckBox *sampleCheck_ = nullptr;
	QCheckBox *longCheck_ = nullptr;
	QPushButton *replayButton_ = nullptr;
	QLabel *zoomLabel_ = nullptr;
	QCheckBox *gridCheck_ = nullptr;
	QSpinBox *gridSpin_ = nullptr;
	QCheckBox *gridSnapCheck_ = nullptr;
	std::vector<QToolButton *> alignButtons_;

	editor::EditorCanvas *canvas_ = nullptr;
	QLabel *hint_ = nullptr;

	QTreeWidget *layers_ = nullptr;
	QToolButton *forwardButton_ = nullptr;
	QToolButton *backwardButton_ = nullptr;
	QToolButton *frontButton_ = nullptr;
	QToolButton *backButton_ = nullptr;

	QLabel *selectedLabel_ = nullptr;
	QWidget *geometryBox_ = nullptr;
	QSpinBox *xSpin_ = nullptr;
	QSpinBox *ySpin_ = nullptr;
	QSpinBox *wSpin_ = nullptr;
	QSpinBox *hSpin_ = nullptr;
	QCheckBox *ownLook_ = nullptr;
	QWidget *fieldsBox_ = nullptr;
	QFormLayout *fieldsForm_ = nullptr;
	std::vector<Bound> bound_;
	std::string builtFor_;
	QLabel *templatePreview_ = nullptr;
	QLineEdit *templateEdit_ = nullptr;

	QTimer *previewTimer_ = nullptr;
	QTimer *syncTimer_ = nullptr;

	std::vector<widget::ElementBox> elements_;
	QStringList layerOrder_;
	QString focus_;
	bool panelSelected_ = false; /* the panel is picked in the layers list */
	QStringList undo_;
	QStringList redo_;
	QString coalesceKey_;
	QElapsedTimer coalesceTimer_;
	std::string lastJson_;
	bool updating_ = false;
	bool styled_ = false;

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

/* Remembered for as long as OBS runs, so reopening the editor keeps them. */
struct ViewSettings {
	bool grid = false;
	int grid_size = 10;
	bool grid_snap = false;
};
ViewSettings view_settings;

WidgetEditor::WidgetEditor(QWidget *parent) : QDialog(parent, Qt::Window)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr_text("Editor.Title"));
	resize(1240, 760);

	QVBoxLayout *root = new QVBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(6);

	/* Top bar: which widget, and the decisions that shape the whole card. */
	QHBoxLayout *bar = new QHBoxLayout();
	bar->setSpacing(8);

	widgetCombo_ = new QComboBox(this);
	widgetCombo_->setMinimumWidth(160);
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

	presetsButton_ = new QToolButton(this);
	presetsButton_->setText(tr_text("Editor.Presets"));
	presetsButton_->setToolTip(tr_text("Editor.Presets.Tip"));
	presetsButton_->setPopupMode(QToolButton::InstantPopup);
	presetsButton_->setMenu(new QMenu(presetsButton_));
	connect(presetsButton_->menu(), &QMenu::aboutToShow, this, &WidgetEditor::showPresetsMenu);

	undoButton_ = new QPushButton(tr_text("Editor.Undo"), this);
	undoButton_->setToolTip(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText));
	redoButton_ = new QPushButton(tr_text("Editor.Redo"), this);
	redoButton_->setToolTip(QKeySequence(QKeySequence::Redo).toString(QKeySequence::NativeText));
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
	bar->addWidget(presetsButton_);
	bar->addWidget(undoButton_);
	bar->addWidget(redoButton_);
	bar->addWidget(propertiesButton_);
	root->addLayout(bar);

	/* Second bar: the sample track, lining things up, and the view. */
	QHBoxLayout *tools = new QHBoxLayout();
	tools->setSpacing(6);

	sampleCheck_ = new QCheckBox(tr_text("Editor.Sample"), this);
	sampleCheck_->setToolTip(tr_text("Editor.Sample.Tip"));
	longCheck_ = new QCheckBox(tr_text("Editor.Sample.Long"), this);
	longCheck_->setToolTip(tr_text("Editor.Sample.Long.Tip"));
	longCheck_->setEnabled(false);
	replayButton_ = new QPushButton(tr_text("Editor.Replay"), this);
	replayButton_->setToolTip(tr_text("Editor.Replay.Tip"));

	tools->addWidget(sampleCheck_);
	tools->addWidget(longCheck_);
	tools->addWidget(replayButton_);
	tools->addSpacing(16);

	const struct {
		Align how;
		const char *tip;
	} ALIGNS[] = {
		{Align::Left, "Editor.Align.Left"},
		{Align::HorizontalCenter, "Editor.Align.Center"},
		{Align::Right, "Editor.Align.Right"},
		{Align::Top, "Editor.Align.Top"},
		{Align::VerticalCenter, "Editor.Align.Middle"},
		{Align::Bottom, "Editor.Align.Bottom"},
	};
	for (const auto &align : ALIGNS) {
		QToolButton *button = new QToolButton(this);
		button->setIcon(align_icon(align.how));
		button->setToolTip(tr_text(align.tip));
		button->setAutoRaise(true);
		const Align how = align.how;
		connect(button, &QToolButton::clicked, this, [this, how]() { alignSelection(how); });
		tools->addWidget(button);
		alignButtons_.push_back(button);
	}
	for (bool horizontal : {true, false}) {
		QToolButton *button = new QToolButton(this);
		button->setIcon(distribute_icon(horizontal));
		button->setToolTip(tr_text(horizontal ? "Editor.Distribute.Horizontal" : "Editor.Distribute.Vertical"));
		button->setAutoRaise(true);
		connect(button, &QToolButton::clicked, this, [this, horizontal]() { distributeSelection(horizontal); });
		tools->addWidget(button);
		alignButtons_.push_back(button);
	}

	tools->addStretch(1);

	gridCheck_ = new QCheckBox(tr_text("Editor.Grid"), this);
	gridSpin_ = new QSpinBox(this);
	gridSpin_->setRange(2, 256);
	gridSpin_->setSuffix(QStringLiteral(" px"));
	gridSnapCheck_ = new QCheckBox(tr_text("Editor.Grid.Snap"), this);
	gridCheck_->setChecked(view_settings.grid);
	gridSpin_->setValue(view_settings.grid_size);
	gridSnapCheck_->setChecked(view_settings.grid_snap);
	tools->addWidget(gridCheck_);
	tools->addWidget(gridSpin_);
	tools->addWidget(gridSnapCheck_);
	tools->addSpacing(12);

	QToolButton *zoomOut = new QToolButton(this);
	zoomOut->setText(QStringLiteral("−"));
	zoomOut->setToolTip(tr_text("Editor.ZoomOut"));
	QToolButton *zoomIn = new QToolButton(this);
	zoomIn->setText(QStringLiteral("+"));
	zoomIn->setToolTip(tr_text("Editor.ZoomIn"));
	QToolButton *zoomFit = new QToolButton(this);
	zoomFit->setText(tr_text("Editor.ZoomFit"));
	zoomLabel_ = new QLabel(QStringLiteral("100%"), this);
	zoomLabel_->setMinimumWidth(44);
	zoomLabel_->setAlignment(Qt::AlignCenter);
	tools->addWidget(zoomOut);
	tools->addWidget(zoomLabel_);
	tools->addWidget(zoomIn);
	tools->addWidget(zoomFit);
	root->addLayout(tools);

	/* The canvas, and beside it the layers and the inspector. */
	QSplitter *split = new QSplitter(Qt::Horizontal, this);

	canvas_ = new editor::EditorCanvas(split);
	split->addWidget(canvas_);

	QWidget *side = new QWidget(split);
	QVBoxLayout *sideLayout = new QVBoxLayout(side);
	sideLayout->setContentsMargins(8, 0, 0, 0);

	QSplitter *sideSplit = new QSplitter(Qt::Vertical, side);
	sideLayout->addWidget(sideSplit, 1);

	/* Layers: front at the top, a tick to show each, a padlock column. */
	QWidget *layersBox = new QWidget(sideSplit);
	QVBoxLayout *layersLayout = new QVBoxLayout(layersBox);
	layersLayout->setContentsMargins(0, 0, 0, 0);

	layers_ = new QTreeWidget(layersBox);
	layers_->setColumnCount(2);
	layers_->setHeaderLabels({tr_text("Editor.Layers"), tr_text("Editor.Lock")});
	layers_->setRootIsDecorated(false);
	layers_->setSelectionMode(QAbstractItemView::ExtendedSelection);
	layers_->header()->setStretchLastSection(false);
	layers_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	layers_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	layers_->setMinimumWidth(260);
	layersLayout->addWidget(layers_, 1);

	QHBoxLayout *stack = new QHBoxLayout();
	const auto stack_button = [layersBox, stack](const char *text, const char *tip) {
		QToolButton *button = new QToolButton(layersBox);
		button->setText(tr_text(text));
		button->setToolTip(tr_text(tip));
		stack->addWidget(button);
		return button;
	};
	frontButton_ = stack_button("Editor.ToFront", "Editor.ToFront.Tip");
	forwardButton_ = stack_button("Editor.Forward", "Editor.Forward.Tip");
	backwardButton_ = stack_button("Editor.Backward", "Editor.Backward.Tip");
	backButton_ = stack_button("Editor.ToBack", "Editor.ToBack.Tip");
	stack->addStretch(1);
	layersLayout->addLayout(stack);

	sideSplit->addWidget(layersBox);

	/* The inspector: position and size, then the element's own options. */
	QScrollArea *scroll = new QScrollArea(sideSplit);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	QWidget *inspector = new QWidget(scroll);
	QVBoxLayout *inspectorLayout = new QVBoxLayout(inspector);
	inspectorLayout->setContentsMargins(0, 4, 4, 4);

	selectedLabel_ = new QLabel(inspector);
	QFont bold = selectedLabel_->font();
	bold.setBold(true);
	selectedLabel_->setFont(bold);
	inspectorLayout->addWidget(selectedLabel_);

	geometryBox_ = new QWidget(inspector);
	QFormLayout *geometry = new QFormLayout(geometryBox_);
	geometry->setContentsMargins(0, 0, 0, 0);
	const auto spin = [this]() {
		QSpinBox *box = new QSpinBox(geometryBox_);
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
	QHBoxLayout *position = new QHBoxLayout();
	position->addWidget(new QLabel(tr_text("Editor.X"), geometryBox_));
	position->addWidget(xSpin_, 1);
	position->addWidget(new QLabel(tr_text("Editor.Y"), geometryBox_));
	position->addWidget(ySpin_, 1);
	QHBoxLayout *size = new QHBoxLayout();
	size->addWidget(new QLabel(tr_text("Editor.Width"), geometryBox_));
	size->addWidget(wSpin_, 1);
	size->addWidget(new QLabel(tr_text("Editor.Height"), geometryBox_));
	size->addWidget(hSpin_, 1);
	geometry->addRow(position);
	geometry->addRow(size);
	inspectorLayout->addWidget(geometryBox_);

	ownLook_ = new QCheckBox(tr_text("Editor.OwnLook"), inspector);
	ownLook_->setToolTip(tr_text("Widget.OwnLook.Description"));
	inspectorLayout->addWidget(ownLook_);

	fieldsBox_ = new QWidget(inspector);
	fieldsForm_ = new QFormLayout(fieldsBox_);
	fieldsForm_->setContentsMargins(0, 0, 0, 0);
	fieldsForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	inspectorLayout->addWidget(fieldsBox_);
	inspectorLayout->addStretch(1);

	scroll->setWidget(inspector);
	sideSplit->addWidget(scroll);
	sideSplit->setStretchFactor(0, 0);
	sideSplit->setStretchFactor(1, 1);
	sideSplit->setSizes({260, 440});

	split->addWidget(side);
	split->setStretchFactor(0, 1);
	split->setStretchFactor(1, 0);
	split->setSizes({880, 340});
	root->addWidget(split, 1);

	hint_ = new QLabel(this);
	hint_->setWordWrap(true);
	root->addWidget(hint_);

	/* Wiring: the top bar. */
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
		edit([value](obs_data_t *settings) { obs_data_set_int(settings, "layout", value); });
	});

	connect(styleCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		const int value = styleCombo_->itemData(index).toInt();
		edit([value](obs_data_t *settings) { obs_data_set_int(settings, "style", value); });
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
			QStringLiteral("size"));
	};
	widthSpin_->setKeyboardTracking(false);
	heightSpin_->setKeyboardTracking(false);
	connect(widthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, size_changed);
	connect(heightSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, size_changed);

	connect(undoButton_, &QPushButton::clicked, this, &WidgetEditor::undo);
	connect(redoButton_, &QPushButton::clicked, this, &WidgetEditor::redo);
	connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, this, &WidgetEditor::undo);
	connect(new QShortcut(QKeySequence::Redo, this), &QShortcut::activated, this, &WidgetEditor::redo);
	/* Ctrl+Y on every platform, whatever the native redo key is. */
	connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this), &QShortcut::activated, this,
		&WidgetEditor::redo);

	connect(propertiesButton_, &QPushButton::clicked, this, [this]() {
		if (obs_source_t *source = acquire()) {
			obs_frontend_open_source_properties(source);
			obs_source_release(source);
		}
	});

	/* The second bar. */
	connect(sampleCheck_, &QCheckBox::toggled, this, [this](bool on) {
		longCheck_->setEnabled(on);
		setSampleTrack(on);
		refresh(true);
	});
	connect(longCheck_, &QCheckBox::toggled, this, [this](bool) { setSampleTrack(sampleCheck_->isChecked()); });
	connect(replayButton_, &QPushButton::clicked, this, [this]() {
		if (obs_source_t *source = acquire()) {
			replay_widget_track_change(source);
			obs_source_release(source);
		}
	});

	const auto grid_changed = [this]() {
		view_settings.grid = gridCheck_->isChecked();
		view_settings.grid_size = gridSpin_->value();
		view_settings.grid_snap = gridSnapCheck_->isChecked();
		canvas_->setGrid(view_settings.grid, view_settings.grid_size, view_settings.grid_snap);
	};
	connect(gridCheck_, &QCheckBox::toggled, this, grid_changed);
	connect(gridSnapCheck_, &QCheckBox::toggled, this, grid_changed);
	connect(gridSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, grid_changed);
	canvas_->setGrid(view_settings.grid, view_settings.grid_size, view_settings.grid_snap);

	connect(zoomIn, &QToolButton::clicked, this, [this]() { canvas_->setZoom(canvas_->zoom() * 1.25); });
	connect(zoomOut, &QToolButton::clicked, this, [this]() { canvas_->setZoom(canvas_->zoom() / 1.25); });
	connect(zoomFit, &QToolButton::clicked, this, [this]() { canvas_->zoomToFit(); });
	connect(canvas_, &editor::EditorCanvas::zoomChanged, this,
		[this](double zoom) { zoomLabel_->setText(QStringLiteral("%1%").arg(std::lround(zoom * 100.0))); });

	/* The canvas. */
	connect(canvas_, &editor::EditorCanvas::selectionChanged, this, [this](const QString &primary) {
		/* Changes the layers list makes come back here; it has already
		 * decided what has the focus. */
		if (updating_)
			return;
		panelSelected_ = false;
		setFocusId(primary);
		refreshLayers();
		refreshInspector(true);
	});
	connect(canvas_, &editor::EditorCanvas::dragStarted, this, [this](const QString &) { onDragStarted(); });
	connect(canvas_, &editor::EditorCanvas::elementsMoved, this, &WidgetEditor::onElementsMoved);

	/* Layers. */
	connect(layers_, &QTreeWidget::itemSelectionChanged, this, &WidgetEditor::onLayerSelection);
	connect(layers_, &QTreeWidget::itemChanged, this, &WidgetEditor::onLayerChanged);
	connect(frontButton_, &QToolButton::clicked, this, [this]() { restack(2); });
	connect(forwardButton_, &QToolButton::clicked, this, [this]() { restack(1); });
	connect(backwardButton_, &QToolButton::clicked, this, [this]() { restack(-1); });
	connect(backButton_, &QToolButton::clicked, this, [this]() { restack(-2); });

	/* Inspector. */
	for (QSpinBox *box : {xSpin_, ySpin_, wSpin_, hSpin_})
		connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this, &WidgetEditor::onInspectorGeometry);

	connect(ownLook_, &QCheckBox::toggled, this, [this](bool on) {
		if (updating_)
			return;
		const std::string prefix = focus_.toStdString();
		if (!has_own_look(prefix))
			return;

		edit([prefix, on](obs_data_t *settings) {
			/* Starting from what the style gave it, so turning this
			 * on changes nothing on screen. */
			if (on) {
				obs_data_t *resolved = widget::resolve_settings(settings);
				for (const Field &field : fields_for(prefix)) {
					if (!field.styled)
						continue;
					const std::string key = prefix + field.suffix;
					obs_data_item_t *item = obs_data_item_byname(resolved, key.c_str());
					if (!item)
						continue;
					switch (field.kind) {
					case FieldKind::Font: {
						obs_data_t *font = obs_data_get_obj(resolved, key.c_str());
						obs_data_set_obj(settings, key.c_str(), font);
						obs_data_release(font);
						break;
					}
					case FieldKind::Bool:
						obs_data_set_bool(settings, key.c_str(), obs_data_item_get_bool(item));
						break;
					case FieldKind::Double:
					case FieldKind::Percent:
						obs_data_set_double(settings, key.c_str(),
								    obs_data_item_get_double(item));
						break;
					default:
						obs_data_set_int(settings, key.c_str(), obs_data_item_get_int(item));
						break;
					}
					obs_data_item_release(&item);
				}
				obs_data_release(resolved);
			}
			obs_data_set_bool(settings, (prefix + "custom").c_str(), on);
		});
	});

	/* The live preview, and picking up changes made elsewhere, such as in
	 * the properties window. */
	previewTimer_ = new QTimer(this);
	previewTimer_->setInterval(33);
	connect(previewTimer_, &QTimer::timeout, this, &WidgetEditor::pullPreview);
	previewTimer_->start();

	syncTimer_ = new QTimer(this);
	syncTimer_->setInterval(400);
	connect(syncTimer_, &QTimer::timeout, this, [this]() { refresh(); });
	syncTimer_->start();

	coalesceTimer_.start();

	obs_add_main_render_callback(renderCallback, this);
}

WidgetEditor::~WidgetEditor()
{
	/* After this returns the graphics thread no longer calls in. */
	obs_remove_main_render_callback(renderCallback, this);

	/* The sample track is only for designing; the widget goes back to the
	 * real music when the editor closes. */
	if (obs_source_t *source = acquire()) {
		set_widget_sample_track(source, false, false);
		obs_source_release(source);
	}

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
	/* The sample track stays with the editor, not with the widget it was
	 * switched on for. */
	if (obs_source_t *previous = acquire()) {
		if (previous != source)
			set_widget_sample_track(previous, false, false);
		obs_source_release(previous);
	}

	obs_weak_source_release(weak_);
	weak_ = source ? obs_source_get_weak_source(source) : nullptr;

	{
		std::lock_guard<std::mutex> lock(captureMutex_);
		obs_weak_source_release(captureSource_);
		captureSource_ = source ? obs_source_get_weak_source(source) : nullptr;
		freshPixels_ = false;
	}

	undo_.clear();
	redo_.clear();
	coalesceKey_.clear();
	lastJson_.clear();
	layerOrder_.clear();
	builtFor_.clear();
	canvas_->setPreview(QImage());
	canvas_->setSelected(QString());
	focus_.clear();

	if (source && sampleCheck_->isChecked())
		set_widget_sample_track(source, true, longCheck_->isChecked());

	populateWidgets();
	refresh(true);
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

void WidgetEditor::setFocusId(const QString &id)
{
	focus_ = id;
}

/* ------------------------------------------------------------------------- */
/* Settings and undo                                                          */
/* ------------------------------------------------------------------------- */

template<typename Fn> void WidgetEditor::edit(Fn change, const QString &undo_key, bool undoable)
{
	obs_source_t *source = acquire();
	if (!source)
		return;

	obs_data_t *settings = obs_source_get_settings(source);
	if (undoable)
		pushUndo(settings, undo_key);

	change(settings);
	obs_source_update(source, settings);

	obs_data_release(settings);
	obs_source_release(source);

	refresh();
}

void WidgetEditor::pushUndo(obs_data_t *settings, const QString &key)
{
	/* Typing into a box or dragging a slider is one step, not one per
	 * value it passes through. */
	if (!key.isEmpty() && key == coalesceKey_ && coalesceTimer_.elapsed() < COALESCE_MS) {
		coalesceTimer_.restart();
		return;
	}

	undo_.append(QString::fromUtf8(obs_data_get_json(settings)));
	while (undo_.size() > MAX_UNDO)
		undo_.removeFirst();
	redo_.clear();

	coalesceKey_ = key;
	coalesceTimer_.restart();
}

void WidgetEditor::restore(const QString &json)
{
	obs_source_t *source = acquire();
	if (!source)
		return;

	obs_data_t *previous = obs_data_create_from_json(json.toUtf8().constData());
	if (previous) {
		obs_source_reset_settings(source, previous);
		obs_data_release(previous);
	}

	obs_source_release(source);
	coalesceKey_.clear();
	refresh(true);
}

void WidgetEditor::undo()
{
	if (undo_.isEmpty())
		return;

	obs_source_t *source = acquire();
	if (!source)
		return;
	obs_data_t *settings = obs_source_get_settings(source);
	redo_.append(QString::fromUtf8(obs_data_get_json(settings)));
	obs_data_release(settings);
	obs_source_release(source);

	restore(undo_.takeLast());
}

void WidgetEditor::redo()
{
	if (redo_.isEmpty())
		return;

	obs_source_t *source = acquire();
	if (!source)
		return;
	obs_data_t *settings = obs_source_get_settings(source);
	undo_.append(QString::fromUtf8(obs_data_get_json(settings)));
	obs_data_release(settings);
	obs_source_release(source);

	const QString next = redo_.takeLast();
	const QStringList keep = redo_;
	restore(next);
	redo_ = keep;
}

/* ------------------------------------------------------------------------- */
/* Refreshing from the settings                                               */
/* ------------------------------------------------------------------------- */

void WidgetEditor::refresh(bool force)
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
								   propertiesButton_, layers_, presetsButton_,
								   sampleCheck_, replayButton_})
			w->setEnabled(false);
		widgetCombo_->clear();
		return;
	}

	for (QWidget *w :
	     std::initializer_list<QWidget *>{layoutCombo_, styleCombo_, widthSpin_, heightSpin_, propertiesButton_,
					      layers_, presetsButton_, sampleCheck_, replayButton_})
		w->setEnabled(true);

	undoButton_->setEnabled(!undo_.isEmpty());
	redoButton_->setEnabled(!redo_.isEmpty());

	obs_data_t *settings = obs_source_get_settings(source);
	const std::string json = obs_data_get_json(settings);

	if (json == lastJson_ && !force) {
		obs_data_release(settings);
		obs_source_release(source);
		refreshInspector(false); /* the template preview follows the music */
		return;
	}
	lastJson_ = json;

	updating_ = true;

	const Arrangement layout = (Arrangement)obs_data_get_int(settings, "layout");
	layoutCombo_->setCurrentIndex(layoutCombo_->findData((int)layout));
	styleCombo_->setCurrentIndex(styleCombo_->findData((int)obs_data_get_int(settings, "style")));
	styled_ = (WidgetStyle)obs_data_get_int(settings, "style") != WidgetStyle::Custom;
	widthSpin_->setValue((int)obs_data_get_int(settings, "width"));
	heightSpin_->setValue((int)obs_data_get_int(settings, "height"));
	canvas_->setCanvasSize(QSize(widthSpin_->value(), heightSpin_->value()));

	elements_ = widget::element_boxes(settings);

	/* The panel is listed but not drawn as a box: it fills the widget. */
	QVector<editor::CanvasElement> canvasElements;
	for (const widget::ElementBox &element : elements_) {
		if (!element.movable)
			continue;
		editor::CanvasElement item;
		item.id = QString::fromStdString(element.prefix);
		item.label = QString::fromStdString(element.label);
		item.rect = QRectF(element.box.x, element.box.y, element.box.width, element.box.height);
		item.visible = element.enabled;
		item.locked = element.locked;
		canvasElements.append(item);
	}
	canvas_->setElements(canvasElements);

	QString hint = tr_text(layout == Arrangement::Free ? "Editor.Hint.Free" : "Editor.Hint.Auto");
	if (sampleCheck_->isChecked())
		hint += QStringLiteral("  ") + tr_text("Editor.Hint.Sample");
	hint_->setText(hint);

	updating_ = false;

	obs_data_release(settings);
	obs_source_release(source);

	refreshLayers();
	refreshInspector(false);
}

void WidgetEditor::refreshLayers()
{
	/* Front first, as a layers panel reads: highest z, and among equals the
	 * one drawn last. */
	std::vector<size_t> order(elements_.size());
	for (size_t i = 0; i < order.size(); i++)
		order[i] = i;
	std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
		if (elements_[a].z != elements_[b].z)
			return elements_[a].z > elements_[b].z;
		return a > b;
	});

	QStringList ids;
	for (size_t index : order)
		ids.append(QString::fromStdString(elements_[index].prefix));

	updating_ = true;
	layers_->blockSignals(true);

	/* Rebuilt only when the order changes; otherwise updated in place, so
	 * the list does not jump back to the top while it is scrolled. */
	if (ids != layerOrder_ || layers_->topLevelItemCount() != (int)order.size()) {
		layers_->clear();
		for (size_t index : order) {
			const widget::ElementBox &element = elements_[index];
			QTreeWidgetItem *item = new QTreeWidgetItem(layers_);
			item->setData(0, Qt::UserRole, QString::fromStdString(element.prefix));
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		}
		layerOrder_ = ids;
	}

	const QStringList selection = canvas_->selection();

	for (int row = 0; row < layers_->topLevelItemCount(); row++) {
		QTreeWidgetItem *item = layers_->topLevelItem(row);
		const std::string prefix = item->data(0, Qt::UserRole).toString().toStdString();
		const widget::ElementBox *element = nullptr;
		for (const widget::ElementBox &candidate : elements_)
			if (candidate.prefix == prefix)
				element = &candidate;
		if (!element)
			continue;

		const QString label = QString::fromStdString(element->label);
		if (item->text(0) != label)
			item->setText(0, label);
		item->setCheckState(0, element->enabled ? Qt::Checked : Qt::Unchecked);
		item->setCheckState(1, element->locked ? Qt::Checked : Qt::Unchecked);

		const QString id = QString::fromStdString(prefix);
		item->setSelected(selection.contains(id) || (id == QStringLiteral("bg_") && panelSelected_));
	}

	layers_->blockSignals(false);
	updating_ = false;

	const bool any = !focus_.isEmpty() || !selection.isEmpty() || panelSelected_;
	for (QToolButton *button : {frontButton_, forwardButton_, backwardButton_, backButton_})
		button->setEnabled(any);

	const int movable = movableSelection().size();
	for (QToolButton *button : alignButtons_)
		button->setEnabled(movable >= 1);
	if (alignButtons_.size() >= 8) {
		alignButtons_[6]->setEnabled(movable >= 3);
		alignButtons_[7]->setEnabled(movable >= 3);
	}
}

void WidgetEditor::onLayerSelection()
{
	if (updating_)
		return;

	QStringList ids;
	for (QTreeWidgetItem *item : layers_->selectedItems())
		ids.append(item->data(0, Qt::UserRole).toString());

	QString current;
	if (QTreeWidgetItem *item = layers_->currentItem())
		current = item->data(0, Qt::UserRole).toString();

	/* The panel can only be picked here; the canvas gets the rest. */
	panelSelected_ = ids.contains(QStringLiteral("bg_"));
	QStringList onCanvas = ids;
	onCanvas.removeAll(QStringLiteral("bg_"));

	updating_ = true;
	canvas_->setSelection(onCanvas);
	updating_ = false;

	focus_ = ids.contains(current) ? current : (ids.isEmpty() ? QString() : ids.last());

	refreshLayers();
	refreshInspector(true);
}

void WidgetEditor::onLayerChanged(QTreeWidgetItem *item, int column)
{
	if (updating_ || !item)
		return;

	const std::string prefix = item->data(0, Qt::UserRole).toString().toStdString();

	if (column == 1) {
		const bool locked = item->checkState(1) == Qt::Checked;
		edit([prefix, locked](obs_data_t *settings) {
			obs_data_set_bool(settings, (prefix + "locked").c_str(), locked);
		});
		return;
	}

	const bool show = item->checkState(0) == Qt::Checked;

	edit([prefix, show](obs_data_t *settings) {
		const Arrangement layout = (Arrangement)obs_data_get_int(settings, "layout");

		/* Lines, the panel and the extras are switched on and off the
		 * same way under every layout. Under an automatic layout the art
		 * and the bar are the layout's to place, so hiding them picks the
		 * layout or bar setting that leaves them out, and showing them
		 * again goes back to what was there before. */
		if (is_line(prefix) || layout == Arrangement::Free || widget::is_always_free(prefix) ||
		    prefix == "bg_") {
			obs_data_set_bool(settings, (prefix + "enable").c_str(), show);
		} else if (prefix == "bar_") {
			if (show) {
				const int64_t before = obs_data_has_user_value(settings, "bar_position_before_off")
							       ? obs_data_get_int(settings, "bar_position_before_off")
							       : (int64_t)widget::BarPlacement::UnderText;
				obs_data_set_int(settings, "bar_position", before);
			} else {
				obs_data_set_int(settings, "bar_position_before_off",
						 obs_data_get_int(settings, "bar_position"));
				obs_data_set_int(settings, "bar_position", (int64_t)widget::BarPlacement::Off);
			}
		} else if (prefix == "art_") {
			if (show) {
				const int64_t before = obs_data_has_user_value(settings, "layout_before_text_only")
							       ? obs_data_get_int(settings, "layout_before_text_only")
							       : (int64_t)Arrangement::ArtLeft;
				obs_data_set_int(settings, "layout",
						 before == (int64_t)Arrangement::TextOnly
							 ? (int64_t)Arrangement::ArtLeft
							 : before);
			} else {
				obs_data_set_int(settings, "layout_before_text_only", (int64_t)layout);
				obs_data_set_int(settings, "layout", (int64_t)Arrangement::TextOnly);
			}
		}
	});
}

void WidgetEditor::restack(int direction)
{
	QStringList chosen;
	for (QTreeWidgetItem *item : layers_->selectedItems())
		chosen.append(item->data(0, Qt::UserRole).toString());
	if (chosen.isEmpty() && !focus_.isEmpty())
		chosen.append(focus_);
	if (chosen.isEmpty())
		return;

	/* Back to front, as set_stacking wants it: the layers list reversed. */
	QStringList order = layerOrder_;
	std::reverse(order.begin(), order.end());

	if (direction == 2 || direction == -2) {
		QStringList rest;
		QStringList moving;
		for (const QString &id : order)
			(chosen.contains(id) ? moving : rest).append(id);
		order = direction == 2 ? rest + moving : moving + rest;
	} else if (direction == 1) {
		for (int i = (int)order.size() - 2; i >= 0; i--)
			if (chosen.contains(order[i]) && !chosen.contains(order[i + 1]))
				order.swapItemsAt(i, i + 1);
	} else {
		for (int i = 1; i < (int)order.size(); i++)
			if (chosen.contains(order[i]) && !chosen.contains(order[i - 1]))
				order.swapItemsAt(i, i - 1);
	}

	std::vector<std::string> stacking;
	for (const QString &id : order)
		stacking.push_back(id.toStdString());

	edit([stacking](obs_data_t *settings) { widget::set_stacking(settings, stacking); });
}

/* ------------------------------------------------------------------------- */
/* The inspector                                                              */
/* ------------------------------------------------------------------------- */

void WidgetEditor::buildInspector()
{
	/* Everything in the form belongs to the previous element. */
	while (fieldsForm_->rowCount() > 0)
		fieldsForm_->removeRow(0);
	bound_.clear();
	templatePreview_ = nullptr;
	templateEdit_ = nullptr;

	const std::string prefix = focus_.toStdString();
	builtFor_ = prefix;

	for (const Field &field : fields_for(prefix)) {
		Bound bound;
		bound.field = field;
		const std::string key = prefix + field.suffix;
		const QString label = tr_text(field.label);
		const bool coalesce = field.kind == FieldKind::Int || field.kind == FieldKind::Double ||
				      field.kind == FieldKind::Percent;

		switch (field.kind) {
		case FieldKind::Bool: {
			QCheckBox *box = new QCheckBox(label, fieldsBox_);
			connect(box, &QCheckBox::toggled, this, [this, prefix, field, key](bool on) {
				if (updating_)
					return;
				setField(
					prefix, field,
					[key, on](obs_data_t *s) { obs_data_set_bool(s, key.c_str(), on); }, false);
			});
			bound.row = box;
			bound.load = [box, key](obs_data_t *r) {
				box->setChecked(obs_data_get_bool(r, key.c_str()));
			};
			fieldsForm_->addRow(box);
			break;
		}

		case FieldKind::Int:
		case FieldKind::Double:
		case FieldKind::Percent: {
			QDoubleSpinBox *box = new QDoubleSpinBox(fieldsBox_);
			box->setRange(field.min, field.max);
			box->setSingleStep(field.step);
			box->setDecimals(field.kind == FieldKind::Int ? 0 : (field.step < 1.0 ? 1 : 0));
			box->setKeyboardTracking(false);
			if (field.unit)
				box->setSuffix(QString::fromUtf8(field.unit));
			const bool integer = field.kind == FieldKind::Int;
			connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
				[this, prefix, field, key, integer, coalesce](double value) {
					if (updating_)
						return;
					setField(
						prefix, field,
						[key, value, integer](obs_data_t *s) {
							if (integer)
								obs_data_set_int(s, key.c_str(), std::lround(value));
							else
								obs_data_set_double(s, key.c_str(), value);
						},
						coalesce);
				});
			bound.row = box;
			bound.load = [box, key, integer](obs_data_t *r) {
				if (box->hasFocus())
					return;
				box->setValue(integer ? (double)obs_data_get_int(r, key.c_str())
						      : obs_data_get_double(r, key.c_str()));
			};
			bound.label = new QLabel(label, fieldsBox_);
			fieldsForm_->addRow(bound.label, box);
			break;
		}

		case FieldKind::Combo: {
			QComboBox *box = new QComboBox(fieldsBox_);
			for (const Choice &choice : field.options)
				box->addItem(tr_text(choice.text), choice.value);
			connect(box, QOverload<int>::of(&QComboBox::activated), this,
				[this, prefix, field, key, box](int index) {
					const int value = box->itemData(index).toInt();
					setField(
						prefix, field,
						[key, value](obs_data_t *s) {
							obs_data_set_int(s, key.c_str(), value);
						},
						false);
				});
			bound.row = box;
			bound.load = [box, key](obs_data_t *r) {
				box->setCurrentIndex(box->findData((int)obs_data_get_int(r, key.c_str())));
			};
			bound.label = new QLabel(label, fieldsBox_);
			fieldsForm_->addRow(bound.label, box);
			break;
		}

		case FieldKind::Color: {
			QPushButton *button = new QPushButton(fieldsBox_);
			button->setIconSize(QSize(28, 14));
			connect(button, &QPushButton::clicked, this, [this, prefix, field, key, button]() {
				const QColor start = button->property("vr_color").value<QColor>();
				const QColor chosen = QColorDialog::getColor(start, this, tr_text(field.label),
									     QColorDialog::ShowAlphaChannel);
				if (!chosen.isValid())
					return;
				const uint32_t value = from_qcolor(chosen);
				setField(
					prefix, field,
					[key, value](obs_data_t *s) { obs_data_set_int(s, key.c_str(), value); },
					false);
			});
			bound.row = button;
			bound.load = [button, key](obs_data_t *r) {
				const uint32_t value = (uint32_t)obs_data_get_int(r, key.c_str());
				button->setIcon(swatch_icon(value));
				button->setText(to_qcolor(value).name(QColor::HexArgb).toUpper());
				button->setProperty("vr_color", to_qcolor(value));
			};
			bound.label = new QLabel(label, fieldsBox_);
			fieldsForm_->addRow(bound.label, button);
			break;
		}

		case FieldKind::Font: {
			QWidget *row = new QWidget(fieldsBox_);
			QHBoxLayout *layout = new QHBoxLayout(row);
			layout->setContentsMargins(0, 0, 0, 0);
			QFontComboBox *face = new QFontComboBox(row);
			QSpinBox *size = new QSpinBox(row);
			size->setRange(4, 512);
			size->setKeyboardTracking(false);
			QToolButton *bold = new QToolButton(row);
			bold->setText(QStringLiteral("B"));
			bold->setCheckable(true);
			QFont boldFont = bold->font();
			boldFont.setBold(true);
			bold->setFont(boldFont);
			QToolButton *italic = new QToolButton(row);
			italic->setText(QStringLiteral("I"));
			italic->setCheckable(true);
			QFont italicFont = italic->font();
			italicFont.setItalic(true);
			italic->setFont(italicFont);
			layout->addWidget(face, 1);
			layout->addWidget(size);
			layout->addWidget(bold);
			layout->addWidget(italic);

			const auto commit = [this, prefix, field, key, face, size, bold, italic]() {
				if (updating_)
					return;
				const std::string family = face->currentFont().family().toStdString();
				const int px = size->value();
				const int64_t flags = (bold->isChecked() ? OBS_FONT_BOLD : 0) |
						      (italic->isChecked() ? OBS_FONT_ITALIC : 0);
				setField(
					prefix, field,
					[key, family, px, flags](obs_data_t *s) {
						obs_data_t *font = obs_data_create();
						obs_data_set_string(font, "face", family.c_str());
						obs_data_set_string(font, "style", "");
						obs_data_set_int(font, "size", px);
						obs_data_set_int(font, "flags", flags);
						obs_data_set_obj(s, key.c_str(), font);
						obs_data_release(font);
					},
					true);
			};
			connect(face, &QFontComboBox::currentFontChanged, this, commit);
			connect(size, QOverload<int>::of(&QSpinBox::valueChanged), this, commit);
			connect(bold, &QToolButton::toggled, this, commit);
			connect(italic, &QToolButton::toggled, this, commit);

			bound.row = row;
			bound.load = [face, size, bold, italic, key](obs_data_t *r) {
				obs_data_t *font = obs_data_get_obj(r, key.c_str());
				if (!font)
					return;
				const QString family = QString::fromUtf8(obs_data_get_string(font, "face"));
				if (!face->hasFocus() && face->currentFont().family() != family)
					face->setCurrentFont(QFont(family));
				if (!size->hasFocus())
					size->setValue((int)obs_data_get_int(font, "size"));
				const int64_t flags = obs_data_get_int(font, "flags");
				bold->setChecked(flags & OBS_FONT_BOLD);
				italic->setChecked(flags & OBS_FONT_ITALIC);
				obs_data_release(font);
			};
			bound.label = new QLabel(label, fieldsBox_);
			fieldsForm_->addRow(bound.label, row);
			break;
		}

		case FieldKind::Template:
		case FieldKind::Path: {
			QWidget *row = new QWidget(fieldsBox_);
			QHBoxLayout *layout = new QHBoxLayout(row);
			layout->setContentsMargins(0, 0, 0, 0);
			QLineEdit *line = new QLineEdit(row);
			QToolButton *button = new QToolButton(row);
			layout->addWidget(line, 1);
			layout->addWidget(button);

			const auto commit = [this, prefix, field, key, line]() {
				if (updating_)
					return;
				const std::string text = line->text().toStdString();
				setField(
					prefix, field,
					[key, text](obs_data_t *s) {
						obs_data_set_string(s, key.c_str(), text.c_str());
					},
					true);
			};
			connect(line, &QLineEdit::editingFinished, this, commit);

			if (field.kind == FieldKind::Template) {
				line->setToolTip(tr_text("Info.Fields"));
				button->setText(QStringLiteral("{ }"));
				button->setToolTip(tr_text("Editor.InsertField"));
				button->setPopupMode(QToolButton::InstantPopup);
				QMenu *menu = new QMenu(button);
				for (const char *const *name = template_fields(); *name; name++) {
					const QString placeholder =
						QStringLiteral("{%1}").arg(QString::fromUtf8(*name));
					const std::string described = std::string("Field.") + *name;
					const QString text = QStringLiteral("%1   %2").arg(
						placeholder, QString::fromUtf8(obs_module_text(described.c_str())));
					QAction *action = menu->addAction(text);
					connect(action, &QAction::triggered, this, [line, placeholder, commit]() {
						line->insert(placeholder);
						line->setFocus();
						commit();
					});
				}
				button->setMenu(menu);

				if (std::strcmp(field.suffix, "format") == 0) {
					templateEdit_ = line;
					templatePreview_ = new QLabel(fieldsBox_);
					templatePreview_->setWordWrap(true);
					templatePreview_->setTextFormat(Qt::PlainText);
					QPalette palette = templatePreview_->palette();
					palette.setColor(QPalette::WindowText,
							 palette.color(QPalette::WindowText).darker(130));
					templatePreview_->setPalette(palette);
					connect(line, &QLineEdit::textEdited, this,
						[this]() { refreshInspector(false); });
				}
			} else {
				button->setText(QStringLiteral("…"));
				connect(button, &QToolButton::clicked, this, [this, line, commit]() {
					const QString path = QFileDialog::getOpenFileName(
						this, tr_text("Bar.Image"), QFileInfo(line->text()).absolutePath(),
						tr_text("Bar.Image.Filter"));
					if (path.isEmpty())
						return;
					line->setText(path);
					commit();
				});
			}

			bound.row = row;
			bound.load = [line, key](obs_data_t *r) {
				if (line->hasFocus())
					return;
				line->setText(QString::fromUtf8(obs_data_get_string(r, key.c_str())));
			};
			bound.label = new QLabel(label, fieldsBox_);
			fieldsForm_->addRow(bound.label, row);
			if (templatePreview_ && templateEdit_ == line)
				fieldsForm_->addRow(QString(), templatePreview_);
			break;
		}
		}

		bound_.push_back(std::move(bound));
	}
}

void WidgetEditor::refreshInspector(bool rebuild)
{
	const std::string id = focus_.toStdString();
	const widget::ElementBox *element = nullptr;
	for (const widget::ElementBox &candidate : elements_)
		if (candidate.prefix == id)
			element = &candidate;

	if (rebuild || builtFor_ != id)
		buildInspector();

	updating_ = true;

	const bool have = element != nullptr;
	const int count = canvas_->selection().size();

	QString title = have ? QString::fromStdString(element->label) : tr_text("Editor.Nothing");
	if (count > 1)
		title += QStringLiteral("  ") + tr_text("Editor.AndMore").arg(count - 1);
	selectedLabel_->setText(title);

	geometryBox_->setVisible(have && element->movable);
	const bool can_move = have && element->enabled && !element->locked;
	for (QSpinBox *box : {xSpin_, ySpin_, wSpin_, hSpin_})
		box->setEnabled(can_move);

	if (have && element->movable) {
		for (const auto &[box, value] :
		     {std::pair{xSpin_, element->box.x}, std::pair{ySpin_, element->box.y},
		      std::pair{wSpin_, element->box.width}, std::pair{hSpin_, element->box.height}}) {
			if (!box->hasFocus())
				box->setValue((int)std::lround(value));
		}
	}

	obs_source_t *source = acquire();
	obs_data_t *settings = source ? obs_source_get_settings(source) : nullptr;
	obs_data_t *resolved = settings ? widget::resolve_settings(settings) : nullptr;

	const bool own = settings && has_own_look(id) && obs_data_get_bool(settings, (id + "custom").c_str());
	ownLook_->setVisible(have && has_own_look(id) && styled_);
	ownLook_->setChecked(own);

	if (resolved) {
		for (Bound &bound : bound_) {
			if (bound.load)
				bound.load(resolved);

			bool shown = true;
			if (bound.field.shown_if)
				shown = obs_data_get_bool(resolved, (id + bound.field.shown_if).c_str());
			bound.row->setVisible(shown);
			if (bound.label)
				bound.label->setVisible(shown);
		}
	}

	/* What the line says right now, with the track the widget is on. */
	if (templatePreview_ && templateEdit_) {
		Snapshot snapshot;
		const bool have_track = source && get_widget_snapshot(source, snapshot);

		TemplateContext context;
		context.snapshot = have_track ? &snapshot : nullptr;
		context.have_snapshot = have_track;
		context.state_playing = obs_module_text("State.Playing");
		context.state_paused = obs_module_text("State.Paused");
		context.state_stopped = obs_module_text("State.Stopped");
		if (settings)
			context.time_format = (TimeFormat)obs_data_get_int(settings, "time_format");

		const std::string out = expand_template(templateEdit_->text().toStdString(), context);
		templatePreview_->setText(have_track ? tr_text("Editor.Shows").arg(QString::fromStdString(out))
						     : tr_text("Editor.NoTrack"));
	}

	obs_data_release(resolved);
	obs_data_release(settings);
	obs_source_release(source);

	updating_ = false;
}

void WidgetEditor::setField(const std::string &prefix, const Field &field,
			    const std::function<void(obs_data_t *)> &write, bool coalesce)
{
	const bool take_over = field.styled && styled_ && has_own_look(prefix);
	const QString key = coalesce ? QStringLiteral("field:%1%2").arg(QString::fromStdString(prefix), field.suffix)
				     : QString();

	edit(
		[prefix, take_over, write](obs_data_t *settings) {
			/* Changing something the style decides means the element
			 * keeps its own look from now on, starting from exactly
			 * what the style was giving it. */
			if (take_over && !obs_data_get_bool(settings, (prefix + "custom").c_str())) {
				obs_data_t *resolved = widget::resolve_settings(settings);
				for (const Field &other : fields_for(prefix)) {
					if (!other.styled)
						continue;
					const std::string k = prefix + other.suffix;
					switch (other.kind) {
					case FieldKind::Font: {
						obs_data_t *font = obs_data_get_obj(resolved, k.c_str());
						obs_data_set_obj(settings, k.c_str(), font);
						obs_data_release(font);
						break;
					}
					case FieldKind::Bool:
						obs_data_set_bool(settings, k.c_str(),
								  obs_data_get_bool(resolved, k.c_str()));
						break;
					case FieldKind::Double:
					case FieldKind::Percent:
						obs_data_set_double(settings, k.c_str(),
								    obs_data_get_double(resolved, k.c_str()));
						break;
					default:
						obs_data_set_int(settings, k.c_str(),
								 obs_data_get_int(resolved, k.c_str()));
						break;
					}
				}
				obs_data_release(resolved);
				obs_data_set_bool(settings, (prefix + "custom").c_str(), true);
			}

			write(settings);
		},
		key);
}

/* ------------------------------------------------------------------------- */
/* Moving things                                                              */
/* ------------------------------------------------------------------------- */

QVector<CanvasMove> WidgetEditor::movableSelection() const
{
	QVector<CanvasMove> out;
	for (const QString &id : canvas_->selection()) {
		for (const widget::ElementBox &element : elements_) {
			if (QString::fromStdString(element.prefix) != id)
				continue;
			if (element.enabled && element.movable && !element.locked)
				out.append({id, QRectF(element.box.x, element.box.y, element.box.width,
						       element.box.height)});
		}
	}
	return out;
}

/* The first change of a drag. An automatic layout becomes free placement here,
 * starting from where everything already is, so nothing jumps; moving only
 * the extras, which the layout never places, leaves the layout alone. */
void WidgetEditor::onDragStarted()
{
	bool layout_owned = false;
	for (const QString &id : canvas_->selection())
		if (!widget::is_always_free(id.toStdString()))
			layout_owned = true;

	edit([layout_owned](obs_data_t *settings) {
		if (layout_owned && (Arrangement)obs_data_get_int(settings, "layout") != Arrangement::Free)
			widget::detach_to_free_placement(settings);
	});
}

void WidgetEditor::applyMoves(const QVector<CanvasMove> &moves, bool undoable)
{
	if (moves.isEmpty())
		return;

	bool layout_owned = false;
	for (const CanvasMove &move : moves)
		if (!widget::is_always_free(move.id.toStdString()))
			layout_owned = true;

	std::vector<std::pair<std::string, Box>> boxes;
	for (const CanvasMove &move : moves) {
		Box box;
		box.x = (float)move.rect.x();
		box.y = (float)move.rect.y();
		box.width = (float)move.rect.width();
		box.height = (float)move.rect.height();
		box.visible = true;
		boxes.emplace_back(move.id.toStdString(), box);
	}

	edit(
		[boxes, layout_owned, undoable](obs_data_t *settings) {
			if (undoable && layout_owned &&
			    (Arrangement)obs_data_get_int(settings, "layout") != Arrangement::Free)
				widget::detach_to_free_placement(settings);
			for (const auto &[prefix, box] : boxes)
				widget::place_element(settings, prefix, box);
		},
		QString(), undoable);
}

void WidgetEditor::onElementsMoved(const QVector<CanvasMove> &moves, bool finished)
{
	UNUSED_PARAMETER(finished);
	applyMoves(moves, false);
}

void WidgetEditor::onInspectorGeometry()
{
	if (updating_ || focus_.isEmpty())
		return;

	const QRectF rect(xSpin_->value(), ySpin_->value(), wSpin_->value(), hSpin_->value());
	const std::string id = focus_.toStdString();

	Box box;
	box.x = (float)rect.x();
	box.y = (float)rect.y();
	box.width = (float)rect.width();
	box.height = (float)rect.height();
	box.visible = true;

	/* One undo step for a run of changes to the same element's boxes. */
	edit(
		[id, box](obs_data_t *settings) {
			if (!widget::is_always_free(id) &&
			    (Arrangement)obs_data_get_int(settings, "layout") != Arrangement::Free)
				widget::detach_to_free_placement(settings);
			widget::place_element(settings, id, box);
		},
		QStringLiteral("geometry:") + focus_);
}

void WidgetEditor::alignSelection(Align how)
{
	const QVector<CanvasMove> selection = movableSelection();
	if (selection.isEmpty())
		return;

	std::vector<editor::Rect> rects;
	for (const CanvasMove &move : selection)
		rects.push_back({(float)move.rect.x(), (float)move.rect.y(), (float)move.rect.width(),
				 (float)move.rect.height()});

	/* One element lines up with the widget; several with each other. */
	const editor::Rect target =
		rects.size() == 1 ? editor::Rect{0.0f, 0.0f, (float)widthSpin_->value(), (float)heightSpin_->value()}
				  : editor::bounds_of(rects);

	const std::vector<editor::Rect> aligned = editor::align_rects(rects, how, target);

	QVector<CanvasMove> moves;
	for (size_t i = 0; i < aligned.size(); i++)
		moves.append({selection[(int)i].id,
			      QRectF(aligned[i].x, aligned[i].y, aligned[i].width, aligned[i].height)});
	applyMoves(moves, true);
}

void WidgetEditor::distributeSelection(bool horizontal)
{
	const QVector<CanvasMove> selection = movableSelection();
	if (selection.size() < 3)
		return;

	std::vector<editor::Rect> rects;
	for (const CanvasMove &move : selection)
		rects.push_back({(float)move.rect.x(), (float)move.rect.y(), (float)move.rect.width(),
				 (float)move.rect.height()});

	const std::vector<editor::Rect> spread = editor::distribute_rects(rects, horizontal);

	QVector<CanvasMove> moves;
	for (size_t i = 0; i < spread.size(); i++)
		moves.append(
			{selection[(int)i].id, QRectF(spread[i].x, spread[i].y, spread[i].width, spread[i].height)});
	applyMoves(moves, true);
}

/* ------------------------------------------------------------------------- */
/* Sample track and designs                                                   */
/* ------------------------------------------------------------------------- */

void WidgetEditor::setSampleTrack(bool on)
{
	if (obs_source_t *source = acquire()) {
		set_widget_sample_track(source, on, longCheck_->isChecked());
		obs_source_release(source);
	}
}

void WidgetEditor::applyDesign(const QString &json, const QString &what)
{
	bool ok = false;
	const QByteArray bytes = json.toUtf8();

	obs_source_t *source = acquire();
	if (!source)
		return;

	/* Checked on a copy first, so a file that is not a design does not
	 * leave an undo step that changes nothing. */
	obs_data_t *probe = obs_data_create();
	ok = widget::apply_design(probe, bytes.constData());
	obs_data_release(probe);
	obs_source_release(source);

	if (!ok) {
		QMessageBox::warning(this, tr_text("Editor.Presets"), tr_text("Editor.Design.Invalid").arg(what));
		return;
	}

	edit([bytes](obs_data_t *settings) { widget::apply_design(settings, bytes.constData()); });
	refresh(true);
}

void WidgetEditor::copyDesignTo(obs_source_t *target)
{
	obs_source_t *source = acquire();
	if (!source || !target)
		return;

	const QString name = QString::fromUtf8(obs_source_get_name(target));
	if (QMessageBox::question(this, tr_text("Editor.CopyTo"), tr_text("Editor.CopyTo.Confirm").arg(name)) !=
	    QMessageBox::Yes) {
		obs_source_release(source);
		return;
	}

	obs_data_t *settings = obs_source_get_settings(source);
	const std::string design = widget::export_design(settings);
	obs_data_release(settings);
	obs_source_release(source);

	obs_data_t *other = obs_source_get_settings(target);
	if (widget::apply_design(other, design.c_str()))
		obs_source_update(target, other);
	obs_data_release(other);
}

void WidgetEditor::showPresetsMenu()
{
	QMenu *menu = presetsButton_->menu();
	menu->clear();

	/* Built in. */
	QMenu *builtin = menu->addMenu(tr_text("Editor.Presets.Builtin"));
	for (const widget::BuiltinDesign &design : widget::builtin_designs()) {
		const QString json = QString::fromStdString(design.json);
		const QString name = tr_text(design.name);
		connect(builtin->addAction(name), &QAction::triggered, this,
			[this, json, name]() { applyDesign(json, name); });
	}

	/* Saved by the user. */
	const QString dir = presets_dir();
	const QStringList files = dir.isEmpty() ? QStringList()
						: QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files,
								      QDir::Name | QDir::IgnoreCase);

	QMenu *mine = menu->addMenu(tr_text("Editor.Presets.Mine"));
	QMenu *remove = nullptr;
	if (files.isEmpty()) {
		mine->addAction(tr_text("Editor.Presets.None"))->setEnabled(false);
	} else {
		remove = new QMenu(tr_text("Editor.Presets.Delete"), mine);
		for (const QString &file : files) {
			const QString path = QDir(dir).filePath(file);
			const QString name = QFileInfo(file).completeBaseName();
			connect(mine->addAction(name), &QAction::triggered, this, [this, path, name]() {
				QFile in(path);
				if (in.open(QIODevice::ReadOnly))
					applyDesign(QString::fromUtf8(in.readAll()), name);
			});
			connect(remove->addAction(name), &QAction::triggered, this, [this, path, name]() {
				if (QMessageBox::question(this, tr_text("Editor.Presets.Delete"),
							  tr_text("Editor.Presets.Delete.Confirm").arg(name)) ==
				    QMessageBox::Yes)
					QFile::remove(path);
			});
		}
		mine->addSeparator();
		mine->addMenu(remove);
	}

	menu->addSeparator();

	connect(menu->addAction(tr_text("Editor.Presets.Save")), &QAction::triggered, this, [this, dir]() {
		bool ok = false;
		QString name = QInputDialog::getText(this, tr_text("Editor.Presets.Save"),
						     tr_text("Editor.Presets.Name"), QLineEdit::Normal, QString(), &ok)
				       .trimmed();
		if (!ok || name.isEmpty() || dir.isEmpty())
			return;

		/* Kept to characters every file system takes. */
		static const QString BAD = QStringLiteral("\\/:*?\"<>|");
		for (QChar &c : name)
			if (BAD.contains(c))
				c = QLatin1Char('-');

		const QString path = QDir(dir).filePath(name + QStringLiteral(".json"));
		if (QFile::exists(path) &&
		    QMessageBox::question(this, tr_text("Editor.Presets.Save"),
					  tr_text("Editor.Presets.Replace").arg(name)) != QMessageBox::Yes)
			return;

		obs_source_t *source = acquire();
		if (!source)
			return;
		obs_data_t *settings = obs_source_get_settings(source);
		const std::string json = widget::export_design(settings);
		obs_data_release(settings);
		obs_source_release(source);

		QFile out(path);
		if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
			out.write(QByteArray::fromStdString(json));
	});

	connect(menu->addAction(tr_text("Editor.Design.Export")), &QAction::triggered, this, [this]() {
		const QString path = QFileDialog::getSaveFileName(this, tr_text("Editor.Design.Export"), QString(),
								  tr_text("Editor.Design.Filter"));
		if (path.isEmpty())
			return;
		obs_source_t *source = acquire();
		if (!source)
			return;
		obs_data_t *settings = obs_source_get_settings(source);
		const std::string json = widget::export_design(settings);
		obs_data_release(settings);
		obs_source_release(source);

		QFile out(path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive) ? path
										      : path + QStringLiteral(".json"));
		if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
		    out.write(QByteArray::fromStdString(json)) < 0)
			QMessageBox::warning(this, tr_text("Editor.Design.Export"),
					     tr_text("Editor.Design.WriteFailed").arg(path));
	});

	connect(menu->addAction(tr_text("Editor.Design.Import")), &QAction::triggered, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(this, tr_text("Editor.Design.Import"), QString(),
								  tr_text("Editor.Design.Filter"));
		if (path.isEmpty())
			return;
		QFile in(path);
		if (!in.open(QIODevice::ReadOnly)) {
			QMessageBox::warning(this, tr_text("Editor.Design.Import"),
					     tr_text("Editor.Design.Invalid").arg(path));
			return;
		}
		applyDesign(QString::fromUtf8(in.readAll()), QFileInfo(path).fileName());
	});

	/* Copying this design onto another widget. */
	QMenu *copy = menu->addMenu(tr_text("Editor.CopyTo"));
	obs_source_t *current = acquire();
	bool any = false;
	for (obs_source_t *other : all_widgets()) {
		if (other != current) {
			any = true;
			const QString uuid = QString::fromUtf8(obs_source_get_uuid(other));
			connect(copy->addAction(QString::fromUtf8(obs_source_get_name(other))), &QAction::triggered,
				this, [this, uuid]() {
					obs_source_t *target = obs_get_source_by_uuid(uuid.toUtf8().constData());
					copyDesignTo(target);
					obs_source_release(target);
				});
		}
		obs_source_release(other);
	}
	obs_source_release(current);
	copy->setEnabled(any);
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
