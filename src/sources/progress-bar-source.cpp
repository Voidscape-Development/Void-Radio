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

#include "sources/progress-bar-source.hpp"
#include "music/music-api.hpp"
#include "render/bar-renderer.hpp"

#include <obs-module.h>

#include <string>

/*
 * The standalone bar is a thin shell around BarRenderer: it owns the link to a
 * music source and its own canvas size, and hands everything else over. The
 * renderer is given an empty settings prefix so the keys stay exactly as they
 * were before it was shared with the Music Widget.
 */

namespace vr {

namespace {

struct ProgressBar {
	obs_source_t *self = nullptr;
	BarRenderer bar;

	MusicLink link;
};

const char *bar_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("ProgressBar");
}

void bar_update(void *data, obs_data_t *settings)
{
	ProgressBar *bar = static_cast<ProgressBar *>(data);

	bar->link.update(settings);
	bar->bar.update(bar->self, settings, std::string());
}

void *bar_create(obs_data_t *settings, obs_source_t *source)
{
	ProgressBar *bar = new ProgressBar();
	bar->self = source;

	bar->bar.create();
	bar_update(bar, settings);

	return bar;
}

void bar_destroy(void *data)
{
	ProgressBar *bar = static_cast<ProgressBar *>(data);

	bar->bar.destroy();
	bar->link.release();

	delete bar;
}

uint32_t bar_get_width(void *data)
{
	return static_cast<ProgressBar *>(data)->bar.width();
}

uint32_t bar_get_height(void *data)
{
	return static_cast<ProgressBar *>(data)->bar.height();
}

void bar_tick(void *data, float seconds)
{
	ProgressBar *bar = static_cast<ProgressBar *>(data);

	/* The link keeps looking for its music source, so a bar loaded before
	 * the source it points at still finds it. */
	bar->link.tick(seconds);

	Snapshot snapshot;
	bool have_snapshot = false;

	if (obs_source_t *source = bar->link.get()) {
		have_snapshot = get_snapshot(source, snapshot);
		obs_source_release(source);
	}

	bar->bar.tick(seconds, have_snapshot ? &snapshot : nullptr);
}

void bar_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);

	ProgressBar *bar = static_cast<ProgressBar *>(data);

	if (bar->bar.hidden())
		return;

	bar->bar.prepare(bar->bar.width(), bar->bar.height());
	bar->bar.render(bar->bar.width(), bar->bar.height());
}

void bar_enum_active_sources(void *data, obs_source_enum_proc_t callback, void *param)
{
	ProgressBar *bar = static_cast<ProgressBar *>(data);
	bar->bar.enum_active_sources(bar->self, callback, param);
}

void bar_defaults(obs_data_t *settings)
{
	BarRenderer::add_defaults(settings, std::string());
}

obs_properties_t *bar_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	MusicLink::add_property(props);

	/* No accent option here: the album colour is worked out by the Music
	 * Widget, which is the only source that decodes cover art. */
	BarRenderer::add_properties(props, std::string(), true, false);

	return props;
}

struct obs_source_info progress_bar_info = {};

} // namespace

void register_progress_bar_source()
{
	progress_bar_info.id = PROGRESS_BAR_SOURCE_ID;
	progress_bar_info.type = OBS_SOURCE_TYPE_INPUT;
	progress_bar_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
	progress_bar_info.icon_type = OBS_ICON_TYPE_MEDIA;
	progress_bar_info.get_name = bar_get_name;
	progress_bar_info.create = bar_create;
	progress_bar_info.destroy = bar_destroy;
	progress_bar_info.update = bar_update;
	progress_bar_info.get_defaults = bar_defaults;
	progress_bar_info.get_properties = bar_properties;
	progress_bar_info.get_width = bar_get_width;
	progress_bar_info.get_height = bar_get_height;
	progress_bar_info.video_tick = bar_tick;
	progress_bar_info.video_render = bar_render;
	progress_bar_info.enum_active_sources = bar_enum_active_sources;

	obs_register_source(&progress_bar_info);
}

} // namespace vr
