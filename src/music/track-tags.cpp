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
#include <cmath>
#include <cstdlib>

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

/* The short names FFmpeg gives codecs, as people expect to read them. */
std::string codec_label(const AVStream *audio)
{
	if (!audio || !audio->codecpar)
		return std::string();

	const char *name = avcodec_get_name(audio->codecpar->codec_id);
	if (!name || !*name)
		return std::string();

	std::string label = name;

	/* PCM variants all read as WAV-style PCM; mp3float is still MP3. */
	if (label.rfind("pcm_", 0) == 0)
		return "PCM";
	if (label.rfind("mp3", 0) == 0)
		return "MP3";

	for (char &c : label)
		c = (char)std::toupper((unsigned char)c);
	return label;
}

std::string bitrate_label(const AVFormatContext *format, const AVStream *audio)
{
	int64_t bits = audio && audio->codecpar ? audio->codecpar->bit_rate : 0;
	if (bits <= 0)
		bits = format->bit_rate;
	if (bits <= 0)
		return std::string();

	return std::to_string((bits + 500) / 1000) + " kbps";
}

std::string samplerate_label(const AVStream *audio)
{
	const int rate = audio && audio->codecpar ? audio->codecpar->sample_rate : 0;
	if (rate <= 0)
		return std::string();

	/* 44100 reads as 44.1 kHz, 48000 as 48 kHz. */
	const int tenths = (rate + 50) / 100;
	std::string out = std::to_string(tenths / 10);
	if (tenths % 10)
		out += "." + std::to_string(tenths % 10);
	return out + " kHz";
}

bool parse_float(const std::string &text, float &out)
{
	const std::string trimmed = trim(text);
	if (trimmed.empty())
		return false;

	char *end = nullptr;
	const double value = std::strtod(trimmed.c_str(), &end);
	if (end == trimmed.c_str() || !std::isfinite(value))
		return false;

	out = (float)value;
	return true;
}

} // namespace

float loudness_from_replaygain(const std::string &gain)
{
	float value = 0.0f;
	return parse_float(gain, value) ? -18.0f - value : NAN;
}

float loudness_from_r128(const std::string &gain)
{
	float value = 0.0f;
	return parse_float(gain, value) ? -23.0f - value / 256.0f : NAN;
}

bool read_tags(const std::string &path, Tags &out)
{
	out = Tags();

	if (path.empty())
		return false;

	FormatPtr format = open_input(path, nullptr, nullptr);
	if (!format)
		return false;

	/* The codec, rate and bit rate need a short look at the stream itself;
	 * a quarter of a second is plenty and keeps a library scan quick. */
	format->max_analyze_duration = AV_TIME_BASE / 4;
	avformat_find_stream_info(format.get(), nullptr);

	int audio_index = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (audio_index < 0) {
		for (unsigned i = 0; i < format->nb_streams; i++) {
			if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
				audio_index = (int)i;
				break;
			}
		}
	}
	const AVStream *audio = audio_index >= 0 ? format->streams[audio_index] : nullptr;

	static const char *const TITLE[] = {"title", nullptr};
	static const char *const ARTIST[] = {"artist", "album_artist", "performer", "author", "composer", nullptr};
	static const char *const ALBUM[] = {"album", nullptr};
	static const char *const GENRE[] = {"genre", nullptr};
	static const char *const DATE[] = {"date", "year", "originaldate", "creation_time", nullptr};
	static const char *const TRACK[] = {"track", "tracknumber", nullptr};
	static const char *const ALBUM_ARTIST[] = {"album_artist", "albumartist", "album artist", nullptr};
	static const char *const COMPOSER[] = {"composer", nullptr};
	static const char *const COMMENT[] = {"comment", "description", nullptr};
	static const char *const BPM[] = {"bpm", "TBPM", "tmpo", nullptr};
	static const char *const DISC[] = {"disc", "discnumber", nullptr};

	out.title = lookup(format.get(), audio, TITLE);
	out.artist = lookup(format.get(), audio, ARTIST);
	out.album = lookup(format.get(), audio, ALBUM);
	out.genre = lookup(format.get(), audio, GENRE);
	out.year = year_of(lookup(format.get(), audio, DATE));
	out.track = track_number(lookup(format.get(), audio, TRACK));
	out.album_artist = lookup(format.get(), audio, ALBUM_ARTIST);
	out.composer = lookup(format.get(), audio, COMPOSER);
	out.comment = lookup(format.get(), audio, COMMENT);
	out.bpm = track_number(lookup(format.get(), audio, BPM));
	out.disc = track_number(lookup(format.get(), audio, DISC));

	/* ReplayGain first, as the more common, then R128 (Opus). */
	static const char *const RG_TRACK[] = {"replaygain_track_gain", nullptr};
	static const char *const RG_ALBUM[] = {"replaygain_album_gain", nullptr};
	static const char *const RG_TRACK_PEAK[] = {"replaygain_track_peak", nullptr};
	static const char *const RG_ALBUM_PEAK[] = {"replaygain_album_peak", nullptr};
	static const char *const R128_TRACK[] = {"r128_track_gain", nullptr};
	static const char *const R128_ALBUM[] = {"r128_album_gain", nullptr};

	out.track_loudness = loudness_from_replaygain(lookup(format.get(), audio, RG_TRACK));
	if (std::isnan(out.track_loudness))
		out.track_loudness = loudness_from_r128(lookup(format.get(), audio, R128_TRACK));

	out.album_loudness = loudness_from_replaygain(lookup(format.get(), audio, RG_ALBUM));
	if (std::isnan(out.album_loudness))
		out.album_loudness = loudness_from_r128(lookup(format.get(), audio, R128_ALBUM));

	float peak = 0.0f;
	if (parse_float(lookup(format.get(), audio, RG_TRACK_PEAK), peak) && peak > 0.0f)
		out.track_peak = peak;
	if (parse_float(lookup(format.get(), audio, RG_ALBUM_PEAK), peak) && peak > 0.0f)
		out.album_peak = peak;

	out.codec = codec_label(audio);
	out.bitrate = bitrate_label(format.get(), audio);
	out.samplerate = samplerate_label(audio);

	return !out.empty();
}

} // namespace vr
