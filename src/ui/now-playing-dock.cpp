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

#include "ui/now-playing-dock.hpp"
#include "music/music-api.hpp"
#include "util/vr-util.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QStyle>
#include <QVBoxLayout>

namespace vr {

namespace {

constexpr const char *DOCK_ID = "void_radio_now_playing";
constexpr int REFRESH_INTERVAL_MS = 150;

NowPlayingDock *dock_instance = nullptr;

QString tr_text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

void frontend_event(enum obs_frontend_event event, void *data)
{
	NowPlayingDock *dock = static_cast<NowPlayingDock *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		dock->refreshSources();
		break;
	default:
		break;
	}
}

} // namespace

NowPlayingDock::NowPlayingDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("VoidRadioNowPlayingDock"));

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(6);

	sourceCombo_ = new QComboBox(this);
	layout->addWidget(sourceCombo_);

	titleLabel_ = new QLabel(tr_text("Dock.NothingPlaying"), this);
	QFont titleFont = titleLabel_->font();
	titleFont.setBold(true);
	titleLabel_->setFont(titleFont);
	titleLabel_->setWordWrap(true);
	layout->addWidget(titleLabel_);

	artistLabel_ = new QLabel(QString(), this);
	artistLabel_->setWordWrap(true);
	layout->addWidget(artistLabel_);

	QHBoxLayout *seekLayout = new QHBoxLayout();
	elapsedLabel_ = new QLabel(QStringLiteral("0:00"), this);
	durationLabel_ = new QLabel(QStringLiteral("0:00"), this);
	seekSlider_ = new QSlider(Qt::Horizontal, this);
	seekSlider_->setRange(0, 1000);
	seekSlider_->setEnabled(false);

	seekLayout->addWidget(elapsedLabel_);
	seekLayout->addWidget(seekSlider_, 1);
	seekLayout->addWidget(durationLabel_);
	layout->addLayout(seekLayout);

	QHBoxLayout *transportLayout = new QHBoxLayout();

	const auto make_button = [this](QStyle::StandardPixmap icon, const char *tooltip) {
		QToolButton *button = new QToolButton(this);
		button->setIcon(style()->standardIcon(icon));
		button->setToolTip(tr_text(tooltip));
		button->setAutoRaise(true);
		return button;
	};

	previousButton_ = make_button(QStyle::SP_MediaSkipBackward, "Dock.Previous");
	playButton_ = make_button(QStyle::SP_MediaPlay, "Dock.PlayPause");
	stopButton_ = make_button(QStyle::SP_MediaStop, "Dock.Stop");
	nextButton_ = make_button(QStyle::SP_MediaSkipForward, "Dock.Next");

	shuffleButton_ = new QToolButton(this);
	shuffleButton_->setText(tr_text("Dock.Shuffle"));
	shuffleButton_->setCheckable(true);
	shuffleButton_->setAutoRaise(true);

	repeatCombo_ = new QComboBox(this);
	repeatCombo_->addItem(tr_text("Music.Repeat.Off"), (int)RepeatMode::Off);
	repeatCombo_->addItem(tr_text("Music.Repeat.All"), (int)RepeatMode::All);
	repeatCombo_->addItem(tr_text("Music.Repeat.One"), (int)RepeatMode::One);

	transportLayout->addWidget(previousButton_);
	transportLayout->addWidget(playButton_);
	transportLayout->addWidget(stopButton_);
	transportLayout->addWidget(nextButton_);
	transportLayout->addSpacing(8);
	transportLayout->addWidget(shuffleButton_);
	transportLayout->addWidget(repeatCombo_, 1);
	layout->addLayout(transportLayout);

	playlistWidget_ = new QListWidget(this);
	playlistWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
	playlistWidget_->setDragDropMode(QAbstractItemView::InternalMove);
	playlistWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
	playlistWidget_->setAlternatingRowColors(true);
	layout->addWidget(playlistWidget_, 1);

	connect(sourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&NowPlayingDock::onSourceChanged);
	connect(previousButton_, &QToolButton::clicked, this, &NowPlayingDock::onPrevious);
	connect(playButton_, &QToolButton::clicked, this, &NowPlayingDock::onPlayPause);
	connect(stopButton_, &QToolButton::clicked, this, &NowPlayingDock::onStop);
	connect(nextButton_, &QToolButton::clicked, this, &NowPlayingDock::onNext);
	connect(shuffleButton_, &QToolButton::toggled, this, &NowPlayingDock::onShuffleToggled);
	connect(repeatCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&NowPlayingDock::onRepeatChanged);
	connect(seekSlider_, &QSlider::sliderPressed, this, &NowPlayingDock::onSeekPressed);
	connect(seekSlider_, &QSlider::sliderReleased, this, &NowPlayingDock::onSeekReleased);
	connect(playlistWidget_, &QListWidget::itemDoubleClicked, this, &NowPlayingDock::onEntryActivated);
	connect(playlistWidget_, &QListWidget::customContextMenuRequested, this, &NowPlayingDock::onEntryMenu);
	connect(playlistWidget_->model(), &QAbstractItemModel::rowsMoved, this, &NowPlayingDock::onPlaylistReordered);

	timer_ = new QTimer(this);
	timer_->setInterval(REFRESH_INTERVAL_MS);
	connect(timer_, &QTimer::timeout, this, &NowPlayingDock::onRefreshTimer);
	timer_->start();

	obs_frontend_add_event_callback(frontend_event, this);

	refreshSources();
}

NowPlayingDock::~NowPlayingDock()
{
	obs_frontend_remove_event_callback(frontend_event, this);

	if (selected_)
		obs_weak_source_release(selected_);
}

obs_source_t *NowPlayingDock::acquireSource() const
{
	return selected_ ? obs_weak_source_get_source(selected_) : nullptr;
}

void NowPlayingDock::setSelectedSource(obs_source_t *source)
{
	if (selected_) {
		obs_weak_source_release(selected_);
		selected_ = nullptr;
	}

	if (source)
		selected_ = obs_source_get_weak_source(source);

	shownPaths_.clear();
	playlistWidget_->clear();
}

void NowPlayingDock::refreshSources()
{
	updating_ = true;

	obs_source_t *previous = acquireSource();
	const QString previous_name = previous ? QString::fromUtf8(obs_source_get_name(previous)) : QString();

	sourceCombo_->clear();

	std::vector<obs_source_t *> sources;
	enum_music_sources(sources);

	for (obs_source_t *source : sources) {
		const char *name = obs_source_get_name(source);
		if (name)
			sourceCombo_->addItem(QString::fromUtf8(name));

		obs_source_release(source);
	}

	int index = previous_name.isEmpty() ? 0 : sourceCombo_->findText(previous_name);
	if (index < 0)
		index = 0;

	sourceCombo_->setCurrentIndex(index);

	updating_ = false;

	if (previous)
		obs_source_release(previous);

	onSourceChanged(sourceCombo_->currentIndex());
}

void NowPlayingDock::onSourceChanged(int index)
{
	if (index < 0 || index >= sourceCombo_->count()) {
		setSelectedSource(nullptr);
		return;
	}

	const QByteArray name = sourceCombo_->itemText(index).toUtf8();
	obs_source_t *source = obs_get_source_by_name(name.constData());

	setSelectedSource(is_music_source(source) ? source : nullptr);

	if (source)
		obs_source_release(source);

	updateTransport();
	updatePlaylist();
}

void NowPlayingDock::onRefreshTimer()
{
	if (!isVisible())
		return;

	updateTransport();
	updatePlaylist();
}

void NowPlayingDock::updateTransport()
{
	obs_source_t *source = acquireSource();

	Snapshot snapshot;
	const bool have = source && get_snapshot(source, snapshot);

	const bool enabled = source != nullptr;
	previousButton_->setEnabled(enabled);
	playButton_->setEnabled(enabled);
	stopButton_->setEnabled(enabled);
	nextButton_->setEnabled(enabled);
	shuffleButton_->setEnabled(enabled);
	repeatCombo_->setEnabled(enabled);

	if (!have) {
		titleLabel_->setText(tr_text("Dock.NothingPlaying"));
		artistLabel_->clear();
		elapsedLabel_->setText(QStringLiteral("0:00"));
		durationLabel_->setText(QStringLiteral("0:00"));
		seekSlider_->setEnabled(false);

		if (!seeking_)
			seekSlider_->setValue(0);

		if (source)
			obs_source_release(source);

		return;
	}

	const bool playing = snapshot.state == PlayState::Playing;

	playButton_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));

	titleLabel_->setText(snapshot.title.empty() ? tr_text("Dock.NothingPlaying")
						    : QString::fromStdString(snapshot.title));
	artistLabel_->setText(QString::fromStdString(snapshot.artist));

	elapsedLabel_->setText(QString::fromStdString(format_time(snapshot.elapsed_ms, TimeFormat::Auto)));
	durationLabel_->setText(snapshot.duration_ms > 0
					? QString::fromStdString(format_time(snapshot.duration_ms, TimeFormat::Auto))
					: QStringLiteral("--:--"));

	seekSlider_->setEnabled(snapshot.seekable);

	if (!seeking_)
		seekSlider_->setValue((int)(snapshot.progress * 1000.0f));

	/* Mode buttons follow the source, which may also be changed from the
	 * properties dialog while the dock is open. */
	updating_ = true;

	obs_data_t *settings = obs_source_get_settings(source);
	if (settings) {
		shuffleButton_->setChecked(obs_data_get_bool(settings, "shuffle"));

		const int repeat = (int)obs_data_get_int(settings, "repeat");
		const int repeat_index = repeatCombo_->findData(repeat);
		if (repeat_index >= 0)
			repeatCombo_->setCurrentIndex(repeat_index);

		obs_data_release(settings);
	}

	updating_ = false;

	obs_source_release(source);
}

void NowPlayingDock::updatePlaylist()
{
	obs_source_t *source = acquireSource();
	if (!source) {
		if (!shownPaths_.empty()) {
			shownPaths_.clear();
			playlistWidget_->clear();
		}
		return;
	}

	std::vector<EntryInfo> entries;
	int current = -1;
	get_entries(source, entries, current);

	std::vector<std::string> paths;
	paths.reserve(entries.size());
	for (const EntryInfo &entry : entries)
		paths.push_back(entry.path);

	/* Only rebuild when the set of tracks actually changed, otherwise the
	 * user's selection would be cleared several times a second. */
	if (paths != shownPaths_) {
		updating_ = true;

		playlistWidget_->clear();

		for (const EntryInfo &entry : entries) {
			QString label = QString::fromStdString(entry.title);
			if (!entry.artist.empty())
				label = QString::fromStdString(entry.artist) + QStringLiteral(" - ") + label;
			if (entry.missing)
				label += QStringLiteral("  [") + tr_text("Dock.Missing") + QStringLiteral("]");

			QListWidgetItem *item = new QListWidgetItem(label, playlistWidget_);
			item->setData(Qt::UserRole, QString::fromStdString(entry.path));
			item->setToolTip(QString::fromStdString(entry.path));
		}

		shownPaths_ = paths;
		updating_ = false;
	}

	for (int row = 0; row < playlistWidget_->count(); row++) {
		QListWidgetItem *item = playlistWidget_->item(row);
		QFont font = item->font();
		const bool is_current = row == current;

		if (font.bold() != is_current) {
			font.setBold(is_current);
			item->setFont(font);
		}
	}

	obs_source_release(source);
}

void NowPlayingDock::onPlayPause()
{
	obs_source_t *source = acquireSource();
	if (!source)
		return;

	transport_toggle_pause(source);
	obs_source_release(source);
}

void NowPlayingDock::onStop()
{
	obs_source_t *source = acquireSource();
	if (!source)
		return;

	transport_stop(source);
	obs_source_release(source);
}

void NowPlayingDock::onNext()
{
	obs_source_t *source = acquireSource();
	if (!source)
		return;

	transport_next(source);
	obs_source_release(source);
}

void NowPlayingDock::onPrevious()
{
	obs_source_t *source = acquireSource();
	if (!source)
		return;

	transport_previous(source);
	obs_source_release(source);
}

void NowPlayingDock::onShuffleToggled(bool checked)
{
	if (updating_)
		return;

	obs_source_t *source = acquireSource();
	if (!source)
		return;

	set_shuffle(source, checked);
	obs_source_release(source);
}

void NowPlayingDock::onRepeatChanged(int index)
{
	if (updating_ || index < 0)
		return;

	obs_source_t *source = acquireSource();
	if (!source)
		return;

	set_repeat(source, (RepeatMode)repeatCombo_->itemData(index).toInt());
	obs_source_release(source);
}

void NowPlayingDock::onSeekPressed()
{
	seeking_ = true;
}

void NowPlayingDock::onSeekReleased()
{
	seeking_ = false;

	obs_source_t *source = acquireSource();
	if (!source)
		return;

	Snapshot snapshot;
	if (get_snapshot(source, snapshot) && snapshot.duration_ms > 0) {
		const double fraction = (double)seekSlider_->value() / 1000.0;
		transport_seek(source, (int64_t)(fraction * (double)snapshot.duration_ms));
	}

	obs_source_release(source);
}

void NowPlayingDock::onEntryActivated(QListWidgetItem *item)
{
	if (!item)
		return;

	obs_source_t *source = acquireSource();
	if (!source)
		return;

	transport_play_index(source, playlistWidget_->row(item));
	obs_source_release(source);
}

void NowPlayingDock::onEntryMenu(const QPoint &position)
{
	QListWidgetItem *item = playlistWidget_->itemAt(position);
	if (!item)
		return;

	const int row = playlistWidget_->row(item);

	QMenu menu(this);
	QAction *play = menu.addAction(tr_text("Dock.Play"));
	QAction *edit = menu.addAction(tr_text("Dock.EditInfo"));
	menu.addSeparator();
	QAction *remove = menu.addAction(tr_text("Dock.Remove"));

	QAction *chosen = menu.exec(playlistWidget_->viewport()->mapToGlobal(position));
	if (!chosen)
		return;

	if (chosen == play) {
		onEntryActivated(item);
		return;
	}

	if (chosen == edit) {
		editEntry(row);
		return;
	}

	if (chosen == remove) {
		obs_source_t *source = acquireSource();
		if (!source)
			return;

		remove_entry(source, item->data(Qt::UserRole).toString().toUtf8().constData());
		obs_source_release(source);
	}
}

void NowPlayingDock::editEntry(int row)
{
	QListWidgetItem *item = playlistWidget_->item(row);
	if (!item)
		return;

	obs_source_t *source = acquireSource();
	if (!source)
		return;

	const std::string path = item->data(Qt::UserRole).toString().toUtf8().constData();

	std::vector<EntryInfo> entries;
	int current = -1;
	get_entries(source, entries, current);

	QString title;
	QString artist;

	for (const EntryInfo &entry : entries) {
		if (entry.path == path) {
			title = QString::fromStdString(entry.title);
			artist = QString::fromStdString(entry.artist);
			break;
		}
	}

	QDialog dialog(this);
	dialog.setWindowTitle(tr_text("Dock.EditInfo"));

	QFormLayout *form = new QFormLayout(&dialog);
	QLineEdit *titleEdit = new QLineEdit(title, &dialog);
	QLineEdit *artistEdit = new QLineEdit(artist, &dialog);

	form->addRow(tr_text("Dock.TrackTitle"), titleEdit);
	form->addRow(tr_text("Dock.TrackArtist"), artistEdit);

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
	form->addRow(buttons);

	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	if (dialog.exec() == QDialog::Accepted) {
		set_entry_override(source, path, titleEdit->text().toUtf8().constData(),
				   artistEdit->text().toUtf8().constData());
	}

	obs_source_release(source);
}

void NowPlayingDock::onPlaylistReordered()
{
	if (updating_)
		return;

	obs_source_t *source = acquireSource();
	if (!source)
		return;

	std::vector<std::string> paths;
	paths.reserve((size_t)playlistWidget_->count());

	for (int row = 0; row < playlistWidget_->count(); row++)
		paths.push_back(playlistWidget_->item(row)->data(Qt::UserRole).toString().toUtf8().constData());

	shownPaths_ = paths;
	set_entry_order(source, paths);

	obs_source_release(source);
}

void register_now_playing_dock()
{
	if (dock_instance)
		return;

	/* No frontend means no QApplication, and constructing widgets would be
	 * fatal rather than merely useless. */
	if (!obs_frontend_get_main_window()) {
		obs_log(LOG_INFO, "no OBS frontend available, skipping the Now Playing dock");
		return;
	}

	dock_instance = new NowPlayingDock();
	dock_instance->setWindowTitle(QString::fromUtf8(obs_module_text("Dock.Title")));

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), dock_instance)) {
		obs_log(LOG_WARNING, "could not register the Now Playing dock");
		delete dock_instance;
		dock_instance = nullptr;
	}
}

void unregister_now_playing_dock()
{
	if (!dock_instance)
		return;

	obs_frontend_remove_dock(DOCK_ID);
	dock_instance = nullptr;
}

} // namespace vr
