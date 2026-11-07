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
/* OBS registration                                                            */
/* -------------------------------------------------------------------------- */

namespace {

const char *source_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MusicSource");
}

void *source_create(obs_data_t *settings, obs_source_t *source)
{
	return new MusicSource(settings, source);
}

void source_destroy(void *data)
{
	delete static_cast<MusicSource *>(data);
}

void source_update(void *data, obs_data_t *settings)
{
	static_cast<MusicSource *>(data)->update(settings);
}

void source_activate(void *data)
{
	static_cast<MusicSource *>(data)->activate();
}

void source_deactivate(void *data)
{
	static_cast<MusicSource *>(data)->deactivate();
}

void source_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "recurse_dirs", true);
	obs_data_set_default_bool(settings, "shuffle", false);
	obs_data_set_default_int(settings, "repeat", (int64_t)RepeatMode::All);
	obs_data_set_default_int(settings, "crossfade_ms", 0);
	obs_data_set_default_int(settings, "gap_ms", 0);
	obs_data_set_default_int(settings, "fade_in_ms", 0);
	obs_data_set_default_int(settings, "fade_out_ms", 0);
	obs_data_set_default_bool(settings, "play_on_start", true);
	obs_data_set_default_bool(settings, "keep_playing_when_hidden", true);
	obs_data_set_default_bool(settings, "skip_on_error", true);
}

obs_properties_t *source_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_editable_list(props, "playlist", obs_module_text("Music.Playlist"),
					 OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS,
					 obs_module_text("Music.Playlist.Filter"), nullptr);

	obs_properties_add_bool(props, "recurse_dirs", obs_module_text("Music.RecurseDirs"));
	obs_properties_add_bool(props, "shuffle", obs_module_text("Music.Shuffle"));

	obs_property_t *repeat = obs_properties_add_list(props, "repeat", obs_module_text("Music.Repeat"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(repeat, obs_module_text("Music.Repeat.Off"), (int64_t)RepeatMode::Off);
	obs_property_list_add_int(repeat, obs_module_text("Music.Repeat.All"), (int64_t)RepeatMode::All);
	obs_property_list_add_int(repeat, obs_module_text("Music.Repeat.One"), (int64_t)RepeatMode::One);

	obs_property_t *crossfade =
		obs_properties_add_int_slider(props, "crossfade_ms", obs_module_text("Music.Crossfade"), 0, 15000, 100);
	obs_property_int_set_suffix(crossfade, " ms");

	obs_property_t *gap =
		obs_properties_add_int_slider(props, "gap_ms", obs_module_text("Music.Gap"), 0, 10000, 100);
	obs_property_int_set_suffix(gap, " ms");
	obs_property_set_long_description(gap, obs_module_text("Music.Gap.Description"));

	obs_property_t *fade_in =
		obs_properties_add_int_slider(props, "fade_in_ms", obs_module_text("Music.FadeIn"), 0, 10000, 100);
	obs_property_int_set_suffix(fade_in, " ms");

	obs_property_t *fade_out =
		obs_properties_add_int_slider(props, "fade_out_ms", obs_module_text("Music.FadeOut"), 0, 10000, 100);
	obs_property_int_set_suffix(fade_out, " ms");

	obs_properties_add_bool(props, "play_on_start", obs_module_text("Music.PlayOnStart"));

	obs_property_t *keep_playing =
		obs_properties_add_bool(props, "keep_playing_when_hidden", obs_module_text("Music.KeepPlaying"));
	obs_property_set_long_description(keep_playing, obs_module_text("Music.KeepPlaying.Description"));

	obs_properties_add_bool(props, "skip_on_error", obs_module_text("Music.SkipOnError"));

	return props;
}

void source_media_play_pause(void *data, bool pause)
{
	MusicSource *music = static_cast<MusicSource *>(data);

	if (pause)
		music->pause();
	else
		music->play();
}

void source_media_restart(void *data)
{
	static_cast<MusicSource *>(data)->restart();
}

void source_media_stop(void *data)
{
	static_cast<MusicSource *>(data)->stop();
}

void source_media_next(void *data)
{
	static_cast<MusicSource *>(data)->next();
}

void source_media_previous(void *data)
{
	static_cast<MusicSource *>(data)->previous();
}

int64_t source_media_get_duration(void *data)
{
	return static_cast<MusicSource *>(data)->media_duration();
}

int64_t source_media_get_time(void *data)
{
	return static_cast<MusicSource *>(data)->media_time();
}

void source_media_set_time(void *data, int64_t ms)
{
	static_cast<MusicSource *>(data)->seek(ms);
}

enum obs_media_state source_media_get_state(void *data)
{
	return static_cast<MusicSource *>(data)->media_state();
}

struct obs_source_info music_source_info = {};

} // namespace

void register_music_source()
{
	music_source_info.id = MUSIC_SOURCE_ID;
	music_source_info.type = OBS_SOURCE_TYPE_INPUT;
	music_source_info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CONTROLLABLE_MEDIA;
	music_source_info.icon_type = OBS_ICON_TYPE_MEDIA;
	music_source_info.get_name = source_get_name;
	music_source_info.create = source_create;
	music_source_info.destroy = source_destroy;
	music_source_info.update = source_update;
	music_source_info.activate = source_activate;
	music_source_info.deactivate = source_deactivate;
	music_source_info.get_defaults = source_defaults;
	music_source_info.get_properties = source_properties;
	music_source_info.media_play_pause = source_media_play_pause;
	music_source_info.media_restart = source_media_restart;
	music_source_info.media_stop = source_media_stop;
	music_source_info.media_next = source_media_next;
	music_source_info.media_previous = source_media_previous;
	music_source_info.media_get_duration = source_media_get_duration;
	music_source_info.media_get_time = source_media_get_time;
	music_source_info.media_set_time = source_media_set_time;
	music_source_info.media_get_state = source_media_get_state;

	obs_register_source(&music_source_info);
}

} // namespace vr
