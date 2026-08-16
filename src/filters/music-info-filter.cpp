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

#include "filters/music-info-filter.hpp"
#include "music/music-api.hpp"
#include "util/text-template.hpp"
#include "util/vr-util.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <string>
#include <vector>

namespace vr {

namespace {

struct MusicInfoFilter {
	obs_source_t *self = nullptr;

	std::string music_source_name;
	obs_weak_source_t *music_weak = nullptr;

	std::string format;
	std::string idle_format;
	TimeFormat time_format = TimeFormat::Auto;

	std::string last_text;
	bool wrote_once = false;
};

void write_text(MusicInfoFilter *filter, const std::string &text)
{
	if (filter->wrote_once && text == filter->last_text)
		return;

	obs_source_t *parent = obs_filter_get_parent(filter->self);
	if (!parent)
		return;

	obs_data_t *settings = obs_source_get_settings(parent);
	if (!settings)
		return;

	obs_data_set_string(settings, "text", text.c_str());
	obs_source_update(parent, settings);
	obs_data_release(settings);

	filter->last_text = text;
	filter->wrote_once = true;
}

const char *filter_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MusicInfo");
}

void filter_update(void *data, obs_data_t *settings)
{
	MusicInfoFilter *filter = static_cast<MusicInfoFilter *>(data);

	filter->format = obs_data_get_string(settings, "format");
	filter->idle_format = obs_data_get_string(settings, "idle_format");
	filter->time_format = (TimeFormat)obs_data_get_int(settings, "time_format");

	const char *name = obs_data_get_string(settings, "music_source");

	if (filter->music_source_name != name || !filter->music_weak) {
		filter->music_source_name = name ? name : "";

		if (filter->music_weak) {
			obs_weak_source_release(filter->music_weak);
			filter->music_weak = nullptr;
		}

		if (!filter->music_source_name.empty()) {
			obs_source_t *source = obs_get_source_by_name(filter->music_source_name.c_str());
			if (source) {
				if (is_music_source(source))
					filter->music_weak = obs_source_get_weak_source(source);
				obs_source_release(source);
			}
		}
	}

	/* Force the next tick to write even if the text is unchanged, so that
	 * editing the template updates the target immediately. */
	filter->wrote_once = false;
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	MusicInfoFilter *filter = new MusicInfoFilter();
	filter->self = source;

	filter_update(filter, settings);

	return filter;
}

void filter_destroy(void *data)
{
	MusicInfoFilter *filter = static_cast<MusicInfoFilter *>(data);

	if (filter->music_weak)
		obs_weak_source_release(filter->music_weak);

	delete filter;
}

void filter_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);

	MusicInfoFilter *filter = static_cast<MusicInfoFilter *>(data);

	if (!obs_source_enabled(filter->self))
		return;

	Snapshot snapshot;
	bool have_snapshot = false;

	if (filter->music_weak) {
		obs_source_t *source = obs_weak_source_get_source(filter->music_weak);
		if (source) {
			have_snapshot = get_snapshot(source, snapshot);
			obs_source_release(source);
		}
	}

	const bool idle = !have_snapshot || snapshot.state == PlayState::Stopped;
	const std::string &format = idle ? filter->idle_format : filter->format;

	TemplateContext context;
	context.snapshot = &snapshot;
	context.have_snapshot = have_snapshot;
	context.time_format = filter->time_format;
	context.state_playing = obs_module_text("State.Playing");
	context.state_paused = obs_module_text("State.Paused");
	context.state_stopped = obs_module_text("State.Stopped");

	write_text(filter, expand_template(format, context));
}

void filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	MusicInfoFilter *filter = static_cast<MusicInfoFilter *>(data);
	obs_source_skip_video_filter(filter->self);
}

void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "format", "{title}[\n{artist}]\n{elapsed} / {duration}");
	obs_data_set_default_string(settings, "idle_format", "");
	obs_data_set_default_int(settings, "time_format", (int64_t)TimeFormat::Auto);
}

bool music_source_list(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(settings);

	obs_property_list_clear(property);
	obs_property_list_add_string(property, obs_module_text("Common.None"), "");

	std::vector<obs_source_t *> sources;
	enum_music_sources(sources);

	for (obs_source_t *source : sources) {
		const char *name = obs_source_get_name(source);
		if (name)
			obs_property_list_add_string(property, name, name);

		obs_source_release(source);
	}

	return true;
}

obs_properties_t *filter_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_property_t *music = obs_properties_add_list(props, "music_source", obs_module_text("Common.MusicSource"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	music_source_list(props, music, nullptr);

	obs_properties_add_text(props, "format", obs_module_text("Info.Format"), OBS_TEXT_MULTILINE);
	obs_properties_add_text(props, "idle_format", obs_module_text("Info.IdleFormat"), OBS_TEXT_MULTILINE);

	obs_property_t *time_format = obs_properties_add_list(props, "time_format", obs_module_text("Info.TimeFormat"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.Auto"), (int64_t)TimeFormat::Auto);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.MSS"), (int64_t)TimeFormat::MSS);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.MMSS"), (int64_t)TimeFormat::MMSS);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.HMMSS"), (int64_t)TimeFormat::HMMSS);
	obs_property_list_add_int(time_format, obs_module_text("Info.TimeFormat.HHMMSS"), (int64_t)TimeFormat::HHMMSS);

	obs_properties_add_text(props, "fields_help", obs_module_text("Info.Fields"), OBS_TEXT_INFO);

	return props;
}

struct obs_source_info music_info_filter_info = {};

} // namespace

void register_music_info_filter()
{
	music_info_filter_info.id = MUSIC_INFO_FILTER_ID;
	music_info_filter_info.type = OBS_SOURCE_TYPE_FILTER;
	music_info_filter_info.output_flags = OBS_SOURCE_VIDEO;
	music_info_filter_info.get_name = filter_get_name;
	music_info_filter_info.create = filter_create;
	music_info_filter_info.destroy = filter_destroy;
	music_info_filter_info.update = filter_update;
	music_info_filter_info.get_defaults = filter_defaults;
	music_info_filter_info.get_properties = filter_properties;
	music_info_filter_info.video_tick = filter_tick;
	music_info_filter_info.video_render = filter_render;

	obs_register_source(&music_info_filter_info);
}

} // namespace vr
