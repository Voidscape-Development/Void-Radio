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

#include "music/source-hotkeys.hpp"
#include "music/music-source.hpp"

#include <obs-module.h>

namespace vr {

struct SourceHotkeys::Binding {
	MusicSource *music;
	void (MusicSource::*action)();
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

namespace {

void hotkey_pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;

	auto *binding = static_cast<SourceHotkeys::Binding *>(data);
	if (binding && binding->music)
		(binding->music->*binding->action)();
}

} // namespace

SourceHotkeys::SourceHotkeys(MusicSource *music)
{
	struct Definition {
		const char *name;
		const char *text;
		void (MusicSource::*action)();
	};

	const Definition definitions[] = {
		{"VoidRadio.PlayPause", "Hotkey.PlayPause", &MusicSource::toggle_pause},
		{"VoidRadio.Stop", "Hotkey.Stop", &MusicSource::stop},
		{"VoidRadio.Next", "Hotkey.Next", &MusicSource::next},
		{"VoidRadio.Previous", "Hotkey.Previous", &MusicSource::previous},
		{"VoidRadio.Restart", "Hotkey.Restart", &MusicSource::restart},
	};

	for (const Definition &definition : definitions) {
		auto binding = std::make_unique<Binding>();
		binding->music = music;
		binding->action = definition.action;
		binding->id = obs_hotkey_register_source(music->source(), definition.name,
							 obs_module_text(definition.text), hotkey_pressed,
							 binding.get());
		bindings_.push_back(std::move(binding));
	}
}

SourceHotkeys::~SourceHotkeys()
{
	for (const auto &binding : bindings_) {
		if (binding->id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(binding->id);
	}
}

} // namespace vr
