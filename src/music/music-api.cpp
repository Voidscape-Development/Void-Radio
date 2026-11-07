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

#include "music/music-source.hpp"

namespace vr {

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

bool is_music_source(obs_source_t *source)
{
	return MusicSource::from_source(source) != nullptr;
}

bool get_snapshot(obs_source_t *source, Snapshot &out)
{
	MusicSource *music = MusicSource::from_source(source);
	return music ? music->snapshot(out) : false;
}

bool get_entries(obs_source_t *source, std::vector<EntryInfo> &out, int &current_index)
{
	MusicSource *music = MusicSource::from_source(source);
	if (!music)
		return false;

	music->entries(out, current_index);
	return true;
}

void transport_play(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->play();
}

void transport_pause(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->pause();
}

void transport_toggle_pause(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->toggle_pause();
}

void transport_stop(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->stop();
}

void transport_next(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->next();
}

void transport_previous(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->previous();
}

void transport_restart(obs_source_t *source)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->restart();
}

void transport_seek(obs_source_t *source, int64_t ms)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->seek(ms);
}

void transport_play_index(obs_source_t *source, int index)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->play_index(index);
}

void set_shuffle(obs_source_t *source, bool shuffle)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->set_shuffle(shuffle);
}

void set_repeat(obs_source_t *source, RepeatMode mode)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->set_repeat(mode);
}

void set_entry_override(obs_source_t *source, const std::string &path, const std::string &title,
			const std::string &artist)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->set_override(path, title, artist);
}

void set_entry_order(obs_source_t *source, const std::vector<std::string> &paths)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->set_order(paths);
}

void remove_entry(obs_source_t *source, const std::string &path)
{
	if (MusicSource *music = MusicSource::from_source(source))
		music->remove_path(path);
}

void enum_music_sources(std::vector<obs_source_t *> &out)
{
	out.clear();

	auto collect = [](void *param, obs_source_t *source) -> bool {
		auto *list = static_cast<std::vector<obs_source_t *> *>(param);

		if (is_music_source(source)) {
			obs_source_t *ref = obs_source_get_ref(source);
			if (ref)
				list->push_back(ref);
		}

		return true;
	};

	obs_enum_sources(collect, &out);
}

} // namespace vr
