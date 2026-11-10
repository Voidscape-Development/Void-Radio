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

/* Ducking: following another source's level and turning the music down while
 * it is loud. */

#include "music/music-source.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vr {

namespace {

/* How often a trigger that cannot be found is looked for again. */
constexpr uint64_t RETRY_MS = 1000;

bool can_trigger(obs_source_t *source, obs_source_t *self)
{
	if (!source || source == self)
		return false;
	if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
		return false;

	/* Another music source would duck under the music, which is never
	 * what anyone wants. */
	const char *id = obs_source_get_unversioned_id(source);
	return !id || strcmp(id, MUSIC_SOURCE_ID) != 0;
}

} // namespace

/* OBS's audio thread, once per block of the trigger's audio. Only the loudest
 * sample matters, so this is kept to a scan and an atomic maximum. */
void MusicSource::duck_capture(void *param, obs_source_t *, const struct audio_data *audio, bool muted)
{
	MusicSource *self = static_cast<MusicSource *>(param);
	if (muted || !audio || !audio->frames)
		return;

	float peak = 0.0f;
	for (size_t plane = 0; plane < MAX_AV_PLANES; plane++) {
		const float *data = reinterpret_cast<const float *>(audio->data[plane]);
		if (!data)
			continue;
		for (uint32_t i = 0; i < audio->frames; i++)
			peak = std::max(peak, std::fabs(data[i]));
	}

	float seen = self->duck_peak_.load();
	while (peak > seen && !self->duck_peak_.compare_exchange_weak(seen, peak)) {
	}
}

void MusicSource::bind_duck_trigger(const std::string &name)
{
	std::lock_guard<std::mutex> lock(duck_mutex_);

	obs_source_t *current = duck_weak_ ? obs_weak_source_get_source(duck_weak_) : nullptr;

	/* Still bound to a living source of that name: nothing to do. */
	if (current && name == duck_name_ && name == obs_source_get_name(current)) {
		obs_source_release(current);
		return;
	}

	if (current)
		obs_source_remove_audio_capture_callback(current, duck_capture, this);
	obs_source_release(current);
	obs_weak_source_release(duck_weak_);
	duck_weak_ = nullptr;
	duck_name_ = name;

	if (name.empty())
		return;

	obs_source_t *target = obs_get_source_by_name(name.c_str());
	if (can_trigger(target, self_)) {
		obs_source_add_audio_capture_callback(target, duck_capture, this);
		duck_weak_ = obs_source_get_weak_source(target);
	}
	obs_source_release(target);
}

void MusicSource::unbind_duck_trigger()
{
	bind_duck_trigger(std::string());
}

/* Engine thread: a trigger loaded after this source, deleted and made again,
 * or renamed back, is picked up within a second. */
void MusicSource::retry_duck_trigger()
{
	std::string name;
	bool bound = false;
	{
		std::lock_guard<std::mutex> lock(duck_mutex_);
		name = duck_name_;
		if (duck_weak_) {
			obs_source_t *current = obs_weak_source_get_source(duck_weak_);
			bound = current != nullptr;
			obs_source_release(current);
		}
	}

	if (!name.empty() && !bound)
		bind_duck_trigger(name);
}

/* Engine thread, once per block, whether or not anything is playing, so the
 * ducker keeps time. */
void MusicSource::apply_duck(float *const *planes, size_t frames)
{
	duck_retry_frames_ += frames;
	if (duck_retry_frames_ >= ms_to_frames((int64_t)RETRY_MS, format_.sample_rate)) {
		duck_retry_frames_ = 0;
		retry_duck_trigger();
	}

	const float peak = duck_peak_.exchange(0.0f);
	const float start = duck_gain_;
	const float end = ducker_.process(peak, frames, format_.sample_rate);
	duck_gain_ = end;
	duck_db_.store(ducker_.gain_db());

	if (!planes || (start >= 0.99999f && end >= 0.99999f))
		return;

	const float step = (end - start) / (float)std::max<size_t>(1, frames);
	for (uint32_t ch = 0; ch < format_.channels; ch++) {
		float *data = planes[ch];
		for (size_t i = 0; i < frames; i++)
			data[i] *= start + step * (float)i;
	}
}

void MusicSource::list_duck_sources(obs_property_t *list, obs_source_t *self)
{
	struct Context {
		obs_property_t *list;
		obs_source_t *self;
	} context{list, self};

	obs_property_list_add_string(list, obs_module_text("Music.Duck.None"), "");

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			Context *c = static_cast<Context *>(param);
			if (can_trigger(source, c->self)) {
				const char *name = obs_source_get_name(source);
				obs_property_list_add_string(c->list, name, name);
			}
			return true;
		},
		&context);
}

} // namespace vr
