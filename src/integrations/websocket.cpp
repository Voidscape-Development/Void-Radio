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

#include "integrations/websocket.hpp"
#include "music/music-api.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace vr {

namespace {

/* The handful of structures obs-websocket's vendor API passes through proc
 * handler calldata. Laid out exactly as its public header lays them out. */
using RequestFn = void (*)(obs_data_t *request, obs_data_t *response, void *priv);

struct RequestCallback {
	RequestFn callback;
	void *priv_data;
};

std::atomic<proc_handler_t *> websocket_ph{nullptr};
std::atomic<void *> vendor{nullptr};

constexpr const char *VENDOR_NAME = "void-radio";

/* ------------------------------------------------------------------------- */
/* Finding the music source a request means                                   */
/* ------------------------------------------------------------------------- */

/* A strong reference, or null with `error` set. */
obs_source_t *target_source(obs_data_t *request, std::string &error)
{
	const char *name = request ? obs_data_get_string(request, "sourceName") : "";
	const char *uuid = request ? obs_data_get_string(request, "sourceUuid") : "";

	obs_source_t *source = nullptr;

	if (uuid && *uuid)
		source = obs_get_source_by_uuid(uuid);
	else if (name && *name)
		source = obs_get_source_by_name(name);
	else {
		std::vector<obs_source_t *> all;
		enum_music_sources(all);
		if (!all.empty())
			source = obs_source_get_ref(all.front());
		for (obs_source_t *s : all)
			obs_source_release(s);

		if (!source) {
			error = "there is no music source";
			return nullptr;
		}
	}

	if (!source || !is_music_source(source)) {
		obs_source_release(source);
		error = "no music source by that name";
		return nullptr;
	}

	return source;
}

const char *state_name(PlayState state)
{
	switch (state) {
	case PlayState::Playing:
		return "playing";
	case PlayState::Paused:
		return "paused";
	default:
		return "stopped";
	}
}

const char *repeat_name(RepeatMode mode)
{
	switch (mode) {
	case RepeatMode::Off:
		return "off";
	case RepeatMode::One:
		return "one";
	default:
		return "all";
	}
}

void fill_now_playing(obs_data_t *out, obs_source_t *source, const Snapshot &s)
{
	obs_data_set_string(out, "sourceName", obs_source_get_name(source));
	obs_data_set_string(out, "sourceUuid", obs_source_get_uuid(source));
	obs_data_set_string(out, "state", state_name(s.state));
	obs_data_set_string(out, "title", s.title.c_str());
	obs_data_set_string(out, "artist", s.artist.c_str());
	obs_data_set_string(out, "album", s.album.c_str());
	obs_data_set_string(out, "albumArtist", s.album_artist.c_str());
	obs_data_set_string(out, "year", s.year.c_str());
	obs_data_set_string(out, "genre", s.genre.c_str());
	obs_data_set_string(out, "path", s.path.c_str());
	obs_data_set_int(out, "elapsedMs", s.elapsed_ms);
	obs_data_set_int(out, "durationMs", s.duration_ms);
	obs_data_set_bool(out, "seekable", s.seekable);
	obs_data_set_int(out, "index", s.index);
	obs_data_set_int(out, "count", s.count);
	obs_data_set_string(out, "nextTitle", s.next_title.c_str());
	obs_data_set_string(out, "nextArtist", s.next_artist.c_str());
}

void fail(obs_data_t *response, const std::string &error)
{
	obs_data_set_bool(response, "ok", false);
	obs_data_set_string(response, "error", error.c_str());
}

/* ------------------------------------------------------------------------- */
/* Requests                                                                   */
/* ------------------------------------------------------------------------- */

void get_music_sources(obs_data_t *, obs_data_t *response, void *)
{
	std::vector<obs_source_t *> all;
	enum_music_sources(all);

	obs_data_array_t *array = obs_data_array_create();
	for (obs_source_t *source : all) {
		Snapshot snapshot;
		get_snapshot(source, snapshot);
		obs_data_t *item = obs_data_create();
		fill_now_playing(item, source, snapshot);
		obs_data_array_push_back(array, item);
		obs_data_release(item);
		obs_source_release(source);
	}

	obs_data_set_array(response, "sources", array);
	obs_data_array_release(array);
	obs_data_set_bool(response, "ok", true);
}

void get_now_playing(obs_data_t *request, obs_data_t *response, void *)
{
	std::string error;
	obs_source_t *source = target_source(request, error);
	if (!source)
		return fail(response, error);

	Snapshot snapshot;
	get_snapshot(source, snapshot);
	fill_now_playing(response, source, snapshot);
	obs_data_set_bool(response, "ok", true);
	obs_source_release(source);
}

void get_playlist(obs_data_t *request, obs_data_t *response, void *)
{
	std::string error;
	obs_source_t *source = target_source(request, error);
	if (!source)
		return fail(response, error);

	std::vector<EntryInfo> entries;
	int current = -1;
	get_entries(source, entries, current);

	obs_data_array_t *array = obs_data_array_create();
	for (size_t i = 0; i < entries.size(); i++) {
		obs_data_t *item = obs_data_create();
		obs_data_set_int(item, "index", (long long)i);
		obs_data_set_string(item, "title", entries[i].title.c_str());
		obs_data_set_string(item, "artist", entries[i].artist.c_str());
		obs_data_set_string(item, "path", entries[i].path.c_str());
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}

	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_bool(response, "shuffle", obs_data_get_bool(settings, "shuffle"));
	obs_data_set_string(response, "repeat", repeat_name((RepeatMode)obs_data_get_int(settings, "repeat")));
	obs_data_release(settings);

	obs_data_set_string(response, "sourceName", obs_source_get_name(source));
	obs_data_set_int(response, "currentIndex", current);
	obs_data_set_array(response, "entries", array);
	obs_data_array_release(array);
	obs_data_set_bool(response, "ok", true);
	obs_source_release(source);
}

/* The plain transport requests, which only need a source. */
template<void (*Action)(obs_source_t *)> void transport(obs_data_t *request, obs_data_t *response, void *)
{
	std::string error;
	obs_source_t *source = target_source(request, error);
	if (!source)
		return fail(response, error);

	Action(source);
	obs_data_set_string(response, "sourceName", obs_source_get_name(source));
	obs_data_set_bool(response, "ok", true);
	obs_source_release(source);
}

void seek(obs_data_t *request, obs_data_t *response, void *)
{
	if (!obs_data_has_user_value(request, "positionMs"))
		return fail(response, "positionMs is required");

	std::string error;
	obs_source_t *source = target_source(request, error);
	if (!source)
		return fail(response, error);

	transport_seek(source, obs_data_get_int(request, "positionMs"));
	obs_data_set_bool(response, "ok", true);
	obs_source_release(source);
}

void play_index(obs_data_t *request, obs_data_t *response, void *)
{
	if (!obs_data_has_user_value(request, "index"))
		return fail(response, "index is required");

	std::string error;
	obs_source_t *source = target_source(request, error);
	if (!source)
		return fail(response, error);

	std::vector<EntryInfo> entries;
	int current = -1;
	get_entries(source, entries, current);

	const long long index = obs_data_get_int(request, "index");
	if (index < 0 || index >= (long long)entries.size()) {
		obs_source_release(source);
		return fail(response, "index is outside the playlist");
	}

	transport_play_index(source, (int)index);
	obs_data_set_bool(response, "ok", true);
	obs_source_release(source);
}

void set_shuffle_request(obs_data_t *request, obs_data_t *response, void *)
{
	if (!obs_data_has_user_value(request, "shuffle"))
		return fail(response, "shuffle is required");

	std::string error;
	obs_source_t *source = target_source(request, error);
	if (!source)
		return fail(response, error);

	set_shuffle(source, obs_data_get_bool(request, "shuffle"));
	obs_data_set_bool(response, "ok", true);
	obs_source_release(source);
}

void set_repeat_request(obs_data_t *request, obs_data_t *response, void *)
{
	const char *mode = obs_data_get_string(request, "mode");
	RepeatMode repeat;
	if (strcmp(mode, "off") == 0)
		repeat = RepeatMode::Off;
	else if (strcmp(mode, "all") == 0)
		repeat = RepeatMode::All;
	else if (strcmp(mode, "one") == 0)
		repeat = RepeatMode::One;
	else
		return fail(response, "mode must be off, all or one");

	std::string error;
	obs_source_t *source = target_source(request, error);
	if (!source)
		return fail(response, error);

	set_repeat(source, repeat);
	obs_data_set_bool(response, "ok", true);
	obs_source_release(source);
}

bool register_request(void *v, const char *name, RequestFn fn)
{
	proc_handler_t *ph = websocket_ph.load();
	RequestCallback callback = {fn, nullptr};

	calldata_t cd = {};
	calldata_set_ptr(&cd, "vendor", v);
	calldata_set_string(&cd, "type", name);
	calldata_set_ptr(&cd, "callback", &callback);
	proc_handler_call(ph, "vendor_request_register", &cd);
	const bool ok = calldata_bool(&cd, "success");
	calldata_free(&cd);
	return ok;
}

/* ------------------------------------------------------------------------- */
/* Events                                                                     */
/* ------------------------------------------------------------------------- */

void emit(const char *name, obs_data_t *data)
{
	proc_handler_t *ph = websocket_ph.load();
	void *v = vendor.load();
	if (!ph || !v)
		return;

	calldata_t cd = {};
	calldata_set_ptr(&cd, "vendor", v);
	calldata_set_string(&cd, "type", name);
	calldata_set_ptr(&cd, "data", data);
	proc_handler_call(ph, "vendor_event_emit", &cd);
	calldata_free(&cd);
}

/* What was last sent for each source, so events only go out on a change. */
struct Sent {
	uint64_t serial = 0;
	PlayState state = PlayState::Stopped;
	std::string title;
	std::string artist;
	bool any = false;
};

std::mutex sent_mutex;
std::map<obs_source_t *, Sent> sent;

} // namespace

void register_websocket_vendor()
{
	calldata_t cd = {};
	if (!proc_handler_call(obs_get_proc_handler(), "obs_websocket_api_get_ph", &cd)) {
		calldata_free(&cd);
		obs_log(LOG_INFO, "obs-websocket is not installed; remote control is off");
		return;
	}
	proc_handler_t *ph = static_cast<proc_handler_t *>(calldata_ptr(&cd, "ph"));
	calldata_free(&cd);

	if (!ph)
		return;
	websocket_ph.store(ph);

	calldata_t reg = {};
	calldata_set_string(&reg, "name", VENDOR_NAME);
	proc_handler_call(ph, "vendor_register", &reg);
	void *v = calldata_ptr(&reg, "vendor");
	calldata_free(&reg);

	if (!v) {
		obs_log(LOG_WARNING, "could not register with obs-websocket");
		return;
	}

	const struct {
		const char *name;
		RequestFn fn;
	} REQUESTS[] = {
		{"GetMusicSources", get_music_sources},
		{"GetNowPlaying", get_now_playing},
		{"GetPlaylist", get_playlist},
		{"Play", transport<transport_play>},
		{"Pause", transport<transport_pause>},
		{"TogglePause", transport<transport_toggle_pause>},
		{"Stop", transport<transport_stop>},
		{"Next", transport<transport_next>},
		{"Previous", transport<transport_previous>},
		{"Restart", transport<transport_restart>},
		{"Seek", seek},
		{"PlayIndex", play_index},
		{"SetShuffle", set_shuffle_request},
		{"SetRepeat", set_repeat_request},
	};

	for (const auto &request : REQUESTS) {
		if (!register_request(v, request.name, request.fn))
			obs_log(LOG_WARNING, "obs-websocket refused the %s request", request.name);
	}

	vendor.store(v);
	obs_log(LOG_INFO, "remote control registered with obs-websocket as \"%s\"", VENDOR_NAME);
}

void websocket_publish(obs_source_t *source, const Snapshot &snapshot)
{
	if (!vendor.load())
		return;

	bool track = false;
	bool state = false;
	{
		std::lock_guard<std::mutex> lock(sent_mutex);
		Sent &last = sent[source];

		/* A new track, or the same track's details arriving after it
		 * started, which is when its tags have been read. */
		track = !last.any || snapshot.track_serial != last.serial || snapshot.title != last.title ||
			snapshot.artist != last.artist;

		/* A source that loads stopped has nothing to announce; one
		 * that starts playing straight away does. */
		state = last.any ? snapshot.state != last.state : snapshot.state != PlayState::Stopped;

		last.any = true;
		last.serial = snapshot.track_serial;
		last.state = snapshot.state;
		last.title = snapshot.title;
		last.artist = snapshot.artist;
	}

	if (!track && !state)
		return;

	obs_data_t *data = obs_data_create();
	fill_now_playing(data, source, snapshot);

	if (track && snapshot.valid && !snapshot.path.empty())
		emit("NowPlayingChanged", data);
	if (state)
		emit("PlaybackStateChanged", data);

	obs_data_release(data);
}

void websocket_forget(obs_source_t *source)
{
	std::lock_guard<std::mutex> lock(sent_mutex);
	sent.erase(source);
}

} // namespace vr
