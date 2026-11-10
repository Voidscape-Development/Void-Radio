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

/* Reading the source's settings into the engine, and the dock's edits that are
 * written back into them. */

#include "music/music-source.hpp"

namespace vr {

namespace {

PlaylistConfig read_playlist_config(obs_data_t *settings)
{
	PlaylistConfig config;
	config.recurse = obs_data_get_bool(settings, "recurse_dirs");

	if (obs_data_array_t *array = obs_data_get_array(settings, "playlist")) {
		const size_t count = obs_data_array_count(array);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(array, i);
			const char *value = obs_data_get_string(item, "value");

			if (value && *value)
				config.paths.emplace_back(value);

			obs_data_release(item);
		}
		obs_data_array_release(array);
	}

	if (obs_data_array_t *array = obs_data_get_array(settings, "overrides")) {
		const size_t count = obs_data_array_count(array);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(array, i);

			PlaylistEntry entry;
			entry.path = obs_data_get_string(item, "path");
			entry.override_title = obs_data_get_string(item, "title");
			entry.override_artist = obs_data_get_string(item, "artist");

			if (!entry.path.empty())
				config.overrides.push_back(std::move(entry));

			obs_data_release(item);
		}
		obs_data_array_release(array);
	}

	return config;
}

PlayerSettings read_player_settings(obs_data_t *settings)
{
	PlayerSettings out;
	out.repeat = (RepeatMode)obs_data_get_int(settings, "repeat");
	out.shuffle = obs_data_get_bool(settings, "shuffle");
	out.crossfade_ms = (int)obs_data_get_int(settings, "crossfade_ms");
	out.gap_ms = (int)obs_data_get_int(settings, "gap_ms");
	out.fade_in_ms = (int)obs_data_get_int(settings, "fade_in_ms");
	out.fade_out_ms = (int)obs_data_get_int(settings, "fade_out_ms");
	out.skip_on_error = obs_data_get_bool(settings, "skip_on_error");

	out.level_mode = (LevelMode)obs_data_get_int(settings, "level_mode");
	out.level_target = (float)obs_data_get_double(settings, "level_target");
	out.level_measure = obs_data_get_bool(settings, "level_measure");
	out.level_no_clip = obs_data_get_bool(settings, "level_no_clip");

	out.trim_silence = obs_data_get_bool(settings, "trim_silence");
	out.trim_threshold_db = (float)obs_data_get_double(settings, "trim_threshold");
	return out;
}

} // namespace

void MusicSource::update(obs_data_t *settings)
{
	PlaylistConfig config = read_playlist_config(settings);

	/* Walking folders is only worth doing when the list itself changed;
	 * moving a fade slider should not rescan a music library. */
	if (!have_config_ || config != playlist_config_) {
		have_config_ = true;
		playlist_config_ = config;
		library_->rebuild(std::move(config));
	}

	const PlayerSettings player_settings = read_player_settings(settings);
	const bool keep_playing = obs_data_get_bool(settings, "keep_playing_when_hidden");

	DuckSettings duck;
	duck.enabled = obs_data_get_bool(settings, "duck_enable");
	duck.threshold_db = (float)obs_data_get_double(settings, "duck_threshold");
	duck.depth_db = (float)obs_data_get_double(settings, "duck_depth");
	duck.attack_ms = (int)obs_data_get_int(settings, "duck_attack_ms");
	duck.hold_ms = (int)obs_data_get_int(settings, "duck_hold_ms");
	duck.release_ms = (int)obs_data_get_int(settings, "duck_release_ms");

	const char *trigger = obs_data_get_string(settings, "duck_source");
	bind_duck_trigger(duck.enabled && trigger ? trigger : "");

	post([this, player_settings, keep_playing, duck]() {
		player_->set_settings(player_settings);
		keep_playing_when_hidden_ = keep_playing;
		ducker_.set(duck);
	});
}

void MusicSource::write_setting_bool(const char *name, bool value)
{
	obs_data_t *data = obs_source_get_settings(self_);
	obs_data_set_bool(data, name, value);
	obs_source_update(self_, data);
	obs_data_release(data);
}

void MusicSource::write_setting_int(const char *name, int64_t value)
{
	obs_data_t *data = obs_source_get_settings(self_);
	obs_data_set_int(data, name, value);
	obs_source_update(self_, data);
	obs_data_release(data);
}

void MusicSource::set_shuffle(bool shuffle)
{
	write_setting_bool("shuffle", shuffle);
}

void MusicSource::set_repeat(RepeatMode mode)
{
	write_setting_int("repeat", (int64_t)mode);
}

void MusicSource::set_override(const std::string &path, const std::string &title, const std::string &artist)
{
	obs_data_t *data = obs_source_get_settings(self_);
	obs_data_array_t *array = obs_data_get_array(data, "overrides");

	if (!array) {
		array = obs_data_array_create();
		obs_data_set_array(data, "overrides", array);
	}

	bool replaced = false;
	const size_t count = obs_data_array_count(array);

	for (size_t i = 0; i < count && !replaced; i++) {
		obs_data_t *item = obs_data_array_item(array, i);

		if (path == obs_data_get_string(item, "path")) {
			obs_data_set_string(item, "title", title.c_str());
			obs_data_set_string(item, "artist", artist.c_str());
			replaced = true;
		}

		obs_data_release(item);
	}

	if (!replaced && (!title.empty() || !artist.empty())) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "path", path.c_str());
		obs_data_set_string(item, "title", title.c_str());
		obs_data_set_string(item, "artist", artist.c_str());
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}

	obs_source_update(self_, data);
	obs_data_array_release(array);
	obs_data_release(data);
}

void MusicSource::set_order(const std::vector<std::string> &paths)
{
	obs_data_t *data = obs_source_get_settings(self_);
	obs_data_array_t *array = obs_data_array_create();

	for (const std::string &path : paths) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "value", path.c_str());
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}

	obs_data_set_array(data, "playlist", array);
	obs_source_update(self_, data);

	obs_data_array_release(array);
	obs_data_release(data);
}

void MusicSource::remove_path(const std::string &path)
{
	std::vector<EntryInfo> current;
	int index;
	entries(current, index);

	std::vector<std::string> paths;
	paths.reserve(current.size());

	for (const EntryInfo &info : current) {
		if (info.path != path)
			paths.push_back(info.path);
	}

	set_order(paths);
}

} // namespace vr
