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

#pragma once

#include <cmath>
#include <string>

namespace vr {

struct Tags {
	std::string title;
	std::string artist;
	std::string album;
	std::string genre;
	std::string year;
	std::string track;

	std::string album_artist;
	std::string composer;
	std::string comment;
	std::string bpm;
	std::string disc;

	/* Technical details, read from the audio stream rather than the tags.
	 * They do not count towards empty(): every file has them. */
	std::string codec;      /* "FLAC", "MP3", "AAC" ... */
	std::string bitrate;    /* "320 kbps", empty when unknown */
	std::string samplerate; /* "44.1 kHz" */

	/* Loudness in LUFS and sample peak (linear, 1 = full scale), from
	 * ReplayGain or R128 tags, or measured. NAN and 0 when unknown. */
	float track_loudness = NAN;
	float album_loudness = NAN;
	float track_peak = 0.0f;
	float album_peak = 0.0f;

	/* Measuring has been tried for this file, so it is not tried again
	 * whether or not it worked. */
	bool loudness_checked = false;

	/* Set on tags handed over before measuring finishes; the same file's
	 * tags come again once it has. */
	bool measuring = false;

	bool empty() const
	{
		return title.empty() && artist.empty() && album.empty() && genre.empty() && year.empty() &&
		       track.empty() && album_artist.empty() && composer.empty() && comment.empty() && bpm.empty() &&
		       disc.empty();
	}
};

/*
 * Reads a file's embedded metadata with FFmpeg, which covers ID3v2 and ID3v1 in
 * MP3, Vorbis comments in FLAC, Ogg Vorbis and Opus, iTunes metadata in MP4 and
 * M4A, RIFF INFO in WAV, and anything else FFmpeg can open. Only the container
 * header is read.
 *
 * The year is cut down to its first four digits and the track number to the
 * part before any "/total". Returns true when at least one field could be
 * filled in. Never throws, and always leaves `out` in a usable state.
 */
bool read_tags(const std::string &path, Tags &out);

/* ReplayGain and R128 tag values into loudness, for testing on their own.
 * "-6.54 dB" against ReplayGain's -18 LUFS reference reads -11.46; an R128
 * gain is Q7.8 against -23 LUFS. NAN when the text is not a number. */
float loudness_from_replaygain(const std::string &gain);
float loudness_from_r128(const std::string &gain);

} // namespace vr
