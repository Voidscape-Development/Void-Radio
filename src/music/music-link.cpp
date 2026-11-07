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

#include "music/music-api.hpp"

#include <obs-module.h>

namespace vr {

/* -------------------------------------------------------------------------- */
/* MusicLink                                                                   */
/* -------------------------------------------------------------------------- */

namespace {

/* A strong reference to `source` if it is a music source, null otherwise. The
 * reference passed in is consumed either way. */
obs_source_t *keep_if_music(obs_source_t *source)
{
	if (!source)
		return nullptr;

	if (is_music_source(source))
		return source;

	obs_source_release(source);
	return nullptr;
}

/* How long an unresolved link waits before looking again. Frequent enough that
 * a source appearing mid-session is picked up without the user noticing, rare
 * enough that a link left on "None but saved" costs nothing. */
constexpr float LINK_RETRY_SECONDS = 0.5f;

} // namespace

MusicLink::~MusicLink()
{
	release();
}

void MusicLink::release()
{
	if (!weak_)
		return;

	obs_weak_source_release(weak_);
	weak_ = nullptr;
}

bool MusicLink::resolve()
{
	obs_source_t *source = nullptr;

	if (!name_.empty())
		source = keep_if_music(obs_get_source_by_name(name_.c_str()));

	/* The UUID is only consulted when the name misses, which is exactly the
	 * case where the source has been renamed behind the link's back. */
	if (!source && !uuid_.empty())
		source = keep_if_music(obs_get_source_by_uuid(uuid_.c_str()));

	if (!source)
		return false;

	release();
	weak_ = obs_source_get_weak_source(source);

	if (const char *name = obs_source_get_name(source))
		name_ = name;

	if (const char *uuid = obs_source_get_uuid(source))
		uuid_ = uuid;

	obs_source_release(source);

	return weak_ != nullptr;
}

void MusicLink::update(obs_data_t *settings)
{
	const char *name = obs_data_get_string(settings, "music_source");
	const char *uuid = obs_data_get_string(settings, "music_source_uuid");

	name_ = name ? name : "";
	uuid_ = uuid ? uuid : "";
	retry_ = 0.0f;

	release();

	/* An empty dropdown is the user saying "none", so a UUID left over from
	 * an earlier selection must not quietly bring the old link back. */
	if (name_.empty()) {
		uuid_.clear();
		obs_data_set_string(settings, "music_source_uuid", "");
		return;
	}

	resolve();

	obs_data_set_string(settings, "music_source", name_.c_str());
	obs_data_set_string(settings, "music_source_uuid", uuid_.c_str());
}

void MusicLink::tick(float seconds)
{
	if (weak_) {
		/* A weak reference outlives the source it points at, so a dead
		 * one has to be noticed here or the link never recovers from
		 * the source being deleted and made again. */
		obs_source_t *source = obs_weak_source_get_source(weak_);

		if (source) {
			obs_source_release(source);
			return;
		}

		release();
	}

	if (name_.empty() && uuid_.empty())
		return;

	retry_ += seconds;

	if (retry_ < LINK_RETRY_SECONDS)
		return;

	retry_ = 0.0f;
	resolve();
}

obs_source_t *MusicLink::get() const
{
	return weak_ ? obs_weak_source_get_source(weak_) : nullptr;
}

void MusicLink::add_property(obs_properties_t *props)
{
	obs_property_t *list = obs_properties_add_list(props, "music_source", obs_module_text("Common.MusicSource"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(list, obs_module_text("Common.None"), "");

	std::vector<obs_source_t *> sources;
	enum_music_sources(sources);

	for (obs_source_t *source : sources) {
		const char *name = obs_source_get_name(source);
		if (name)
			obs_property_list_add_string(list, name, name);

		obs_source_release(source);
	}
}

} // namespace vr
