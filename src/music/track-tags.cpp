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

#include "music/track-tags.hpp"
#include "audio/ffmpeg-util.hpp"
#include "util/vr-util.hpp"

#include <cctype>

namespace vr {

namespace {

/* Formats such as Ogg keep their comments on the audio stream rather than the
 * container, and some files carry both, so the container is asked first and
 * the audio stream fills whatever it left out. */
std::string lookup(const AVFormatContext *format, const AVStream *audio, const char *const *keys)
{
	for (const AVDictionary *dict :
	     {(const AVDictionary *)format->metadata, audio ? (const AVDictionary *)audio->metadata : nullptr}) {
		if (!dict)
			continue;

		for (const char *const *key = keys; *key; key++) {
			const AVDictionaryEntry *entry = av_dict_get(dict, *key, nullptr, 0);
			if (entry && entry->value) {
				const std::string value = trim(entry->value);
				if (!value.empty())
					return value;
			}
		}
	}

	return std::string();
}

/* "2024-05-01", "2024" or "2024/05/01" all come out as "2024". */
std::string year_of(const std::string &date)
{
	if (date.size() >= 4 && std::isdigit((unsigned char)date[0]) && std::isdigit((unsigned char)date[1]) &&
	    std::isdigit((unsigned char)date[2]) && std::isdigit((unsigned char)date[3]))
		return date.substr(0, 4);

	return date;
}

/* "03/12" comes out as "3". */
std::string track_number(const std::string &track)
{
	std::string number = trim(track.substr(0, track.find('/')));

	const size_t first = number.find_first_not_of('0');
	if (first == std::string::npos)
		return number.empty() ? number : std::string("0");

	return number.substr(first);
}

} // namespace

bool read_tags(const std::string &path, Tags &out)
{
	out = Tags();

	if (path.empty() || is_url(path))
		return false;

	FormatPtr format = open_input(path, false, nullptr, nullptr);
	if (!format)
		return false;

	const int audio_index = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	const AVStream *audio = audio_index >= 0 ? format->streams[audio_index] : nullptr;

	static const char *const TITLE[] = {"title", nullptr};
	static const char *const ARTIST[] = {"artist", "album_artist", "performer", "author", "composer", nullptr};
	static const char *const ALBUM[] = {"album", nullptr};
	static const char *const GENRE[] = {"genre", nullptr};
	static const char *const DATE[] = {"date", "year", "originaldate", "creation_time", nullptr};
	static const char *const TRACK[] = {"track", "tracknumber", nullptr};

	out.title = lookup(format.get(), audio, TITLE);
	out.artist = lookup(format.get(), audio, ARTIST);
	out.album = lookup(format.get(), audio, ALBUM);
	out.genre = lookup(format.get(), audio, GENRE);
	out.year = year_of(lookup(format.get(), audio, DATE));
	out.track = track_number(lookup(format.get(), audio, TRACK));

	return !out.empty();
}

} // namespace vr
