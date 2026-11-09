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

	obs_data_set_default_int(settings, "level_mode", (int64_t)LevelMode::Off);
	obs_data_set_default_double(settings, "level_target", -16.0);
	obs_data_set_default_bool(settings, "level_measure", true);
	obs_data_set_default_bool(settings, "level_no_clip", true);

	obs_data_set_default_bool(settings, "trim_silence", false);
	obs_data_set_default_double(settings, "trim_threshold", -50.0);

	obs_data_set_default_bool(settings, "duck_enable", false);
	obs_data_set_default_string(settings, "duck_source", "");
	obs_data_set_default_double(settings, "duck_threshold", -35.0);
	obs_data_set_default_double(settings, "duck_depth", 12.0);
	obs_data_set_default_int(settings, "duck_attack_ms", 80);
	obs_data_set_default_int(settings, "duck_hold_ms", 500);
	obs_data_set_default_int(settings, "duck_release_ms", 800);
}

bool level_mode_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool on = (LevelMode)obs_data_get_int(settings, "level_mode") != LevelMode::Off;
	for (const char *name : {"level_target", "level_measure", "level_no_clip"})
		obs_property_set_visible(obs_properties_get(props, name), on);
	return true;
}

bool trim_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, "trim_threshold"),
				 obs_data_get_bool(settings, "trim_silence"));
	return true;
}

void add_level_group(obs_properties_t *props)
{
	obs_properties_t *group = obs_properties_create();

	obs_property_t *mode = obs_properties_add_list(group, "level_mode", obs_module_text("Music.Level"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, obs_module_text("Music.Level.Off"), (int64_t)LevelMode::Off);
	obs_property_list_add_int(mode, obs_module_text("Music.Level.Track"), (int64_t)LevelMode::Track);
	obs_property_list_add_int(mode, obs_module_text("Music.Level.Album"), (int64_t)LevelMode::Album);
	obs_property_set_long_description(mode, obs_module_text("Music.Level.Description"));
	obs_property_set_modified_callback(mode, level_mode_changed);

	obs_property_t *target = obs_properties_add_float_slider(
		group, "level_target", obs_module_text("Music.Level.Target"), -30.0, -6.0, 0.5);
	obs_property_float_set_suffix(target, " LUFS");
	obs_property_set_long_description(target, obs_module_text("Music.Level.Target.Description"));

	obs_property_t *measure =
		obs_properties_add_bool(group, "level_measure", obs_module_text("Music.Level.Measure"));
	obs_property_set_long_description(measure, obs_module_text("Music.Level.Measure.Description"));

	obs_property_t *no_clip =
		obs_properties_add_bool(group, "level_no_clip", obs_module_text("Music.Level.NoClip"));
	obs_property_set_long_description(no_clip, obs_module_text("Music.Level.NoClip.Description"));

	obs_property_t *trim = obs_properties_add_bool(group, "trim_silence", obs_module_text("Music.Trim"));
	obs_property_set_long_description(trim, obs_module_text("Music.Trim.Description"));
	obs_property_set_modified_callback(trim, trim_changed);

	obs_property_t *threshold = obs_properties_add_float_slider(
		group, "trim_threshold", obs_module_text("Music.Trim.Threshold"), -80.0, -20.0, 1.0);
	obs_property_float_set_suffix(threshold, " dB");

	obs_properties_add_group(props, "level_group", obs_module_text("Music.Group.Level"), OBS_GROUP_NORMAL, group);
}

void add_duck_group(obs_properties_t *props, MusicSource *music)
{
	obs_properties_t *group = obs_properties_create();

	obs_property_t *source = obs_properties_add_list(group, "duck_source", obs_module_text("Music.Duck.Source"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	MusicSource::list_duck_sources(source, music ? music->source() : nullptr);
	obs_property_set_long_description(source, obs_module_text("Music.Duck.Source.Description"));

	obs_property_t *threshold = obs_properties_add_float_slider(
		group, "duck_threshold", obs_module_text("Music.Duck.Threshold"), -60.0, 0.0, 1.0);
	obs_property_float_set_suffix(threshold, " dB");
	obs_property_set_long_description(threshold, obs_module_text("Music.Duck.Threshold.Description"));

	obs_property_t *depth = obs_properties_add_float_slider(group, "duck_depth",
								obs_module_text("Music.Duck.Depth"), 0.0, 40.0, 0.5);
	obs_property_float_set_suffix(depth, " dB");

	obs_property_t *attack = obs_properties_add_int_slider(group, "duck_attack_ms",
							       obs_module_text("Music.Duck.Attack"), 0, 2000, 10);
	obs_property_int_set_suffix(attack, " ms");

	obs_property_t *hold =
		obs_properties_add_int_slider(group, "duck_hold_ms", obs_module_text("Music.Duck.Hold"), 0, 5000, 50);
	obs_property_int_set_suffix(hold, " ms");

	obs_property_t *release = obs_properties_add_int_slider(group, "duck_release_ms",
								obs_module_text("Music.Duck.Release"), 0, 5000, 50);
	obs_property_int_set_suffix(release, " ms");

	obs_properties_add_group(props, "duck_enable", obs_module_text("Music.Group.Duck"), OBS_GROUP_CHECKABLE, group);
}

obs_properties_t *source_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_editable_list(props, "playlist", obs_module_text("Music.Playlist"),
					 OBS_EDITABLE_LIST_TYPE_FILES, obs_module_text("Music.Playlist.Filter"),
					 nullptr);

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

	add_level_group(props);
	add_duck_group(props, static_cast<MusicSource *>(data));

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
