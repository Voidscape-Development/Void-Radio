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

#include <obs.h>

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <string>
#include <vector>

namespace vr {

/*
 * Transport panel for the music sources in the current scene collection.
 *
 * Everything the dock shows is polled from the selected source's snapshot on a
 * timer; the dock holds only a weak reference so a deleted source degrades to
 * an empty panel rather than a dangling pointer.
 */
class NowPlayingDock : public QWidget {
	Q_OBJECT

public:
	explicit NowPlayingDock(QWidget *parent = nullptr);
	~NowPlayingDock() override;

	/* Rebuilds the source list, e.g. after a scene collection change. */
	void refreshSources();

private slots:
	void onRefreshTimer();
	void onSourceChanged(int index);
	void onPlayPause();
	void onStop();
	void onNext();
	void onPrevious();
	void onShuffleToggled(bool checked);
	void onRepeatChanged(int index);
	void onSeekPressed();
	void onSeekReleased();
	void onEntryActivated(QListWidgetItem *item);
	void onEntryMenu(const QPoint &position);
	void onPlaylistReordered();

private:
	obs_source_t *acquireSource() const;
	void setSelectedSource(obs_source_t *source);
	void updateTransport();
	void updatePlaylist();
	void editEntry(int row);

	QComboBox *sourceCombo_ = nullptr;
	QLabel *titleLabel_ = nullptr;
	QLabel *artistLabel_ = nullptr;
	QLabel *elapsedLabel_ = nullptr;
	QLabel *durationLabel_ = nullptr;
	QSlider *seekSlider_ = nullptr;
	QToolButton *previousButton_ = nullptr;
	QToolButton *playButton_ = nullptr;
	QToolButton *stopButton_ = nullptr;
	QToolButton *nextButton_ = nullptr;
	QToolButton *shuffleButton_ = nullptr;
	QComboBox *repeatCombo_ = nullptr;
	QListWidget *playlistWidget_ = nullptr;
	QTimer *timer_ = nullptr;

	obs_weak_source_t *selected_ = nullptr;

	std::vector<std::string> shownPaths_;
	bool seeking_ = false;
	bool updating_ = false;
};

/* Creates the dock and registers it with the OBS frontend. */
void register_now_playing_dock();
void unregister_now_playing_dock();

} // namespace vr
