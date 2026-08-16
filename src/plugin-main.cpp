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

#include <obs-module.h>
#include <plugin-support.h>

#include "filters/music-info-filter.hpp"
#include "music/music-source.hpp"
#include "sources/progress-bar-source.hpp"
#include "ui/now-playing-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("Plugin.Name");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}

bool obs_module_load(void)
{
	vr::MusicSource::register_type();
	vr::register_progress_bar_source();
	vr::register_music_info_filter();

	obs_log(LOG_INFO, "Void Radio loaded (version %s)", PLUGIN_VERSION);

	return true;
}

/* The frontend is only guaranteed to exist once every module has loaded, so
 * the dock is registered here rather than in obs_module_load. */
void obs_module_post_load(void)
{
	vr::register_now_playing_dock();
}

void obs_module_unload(void)
{
	vr::unregister_now_playing_dock();

	obs_log(LOG_INFO, "Void Radio unloaded");
}
