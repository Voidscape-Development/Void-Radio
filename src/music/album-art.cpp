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

#include "music/album-art.hpp"
#include "audio/ffmpeg-util.hpp"
#include "util/vr-util.hpp"

#include <util/platform.h>

namespace vr {

namespace {

/* ------------------------------------------------------------------------ */
/* Embedded artwork                                                         */
/* ------------------------------------------------------------------------ */

std::string mime_for(enum AVCodecID id)
{
	switch (id) {
	case AV_CODEC_ID_MJPEG:
		return "image/jpeg";
	case AV_CODEC_ID_PNG:
		return "image/png";
	case AV_CODEC_ID_BMP:
		return "image/bmp";
	case AV_CODEC_ID_GIF:
		return "image/gif";
	case AV_CODEC_ID_WEBP:
		return "image/webp";
	default:
		return std::string();
	}
}

/* FFmpeg names the picture type in the stream's "comment", using the ID3 and
 * FLAC picture type names. */
bool is_front_cover(const AVStream *stream)
{
	const AVDictionaryEntry *comment = av_dict_get(stream->metadata, "comment", nullptr, 0);
	return comment && iequals(comment->value, "Cover (front)");
}

/* ------------------------------------------------------------------------ */
/* Sidecar artwork                                                          */
/* ------------------------------------------------------------------------ */

std::string directory_of(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos)
		return std::string(".");

	if (slash == 0)
		return path.substr(0, 1);

	return path.substr(0, slash);
}

/* Ordered best first, so a file named "cover" beats one named "thumb". */
const char *const SIDECAR_STEMS[] = {"cover", "folder", "front", "album", "albumart", "artwork", "thumb"};

const char *const SIDECAR_EXTENSIONS[] = {"png", "jpg", "jpeg", "webp", "bmp", "gif"};

int sidecar_rank(const std::string &name)
{
	const std::string lower = to_lower(name);

	const size_t dot = lower.find_last_of('.');
	if (dot == std::string::npos || dot == 0)
		return -1;

	const std::string stem = lower.substr(0, dot);
	const std::string extension = lower.substr(dot + 1);

	bool known_extension = false;
	for (const char *candidate : SIDECAR_EXTENSIONS) {
		if (extension == candidate) {
			known_extension = true;
			break;
		}
	}

	if (!known_extension)
		return -1;

	for (size_t i = 0; i < sizeof(SIDECAR_STEMS) / sizeof(SIDECAR_STEMS[0]); i++) {
		if (stem == SIDECAR_STEMS[i])
			return (int)i;
	}

	return -1;
}

} // namespace

/*
 * FFmpeg turns every embedded picture it understands - ID3v2 APIC, FLAC and
 * Vorbis-comment PICTURE blocks, the MP4 "covr" atom - into a stream marked as
 * an attached picture, so one walk over the streams covers every container.
 * Only the header is read; the audio is never touched.
 */
bool read_embedded_art(const std::string &path, AlbumArtData &out)
{
	out.bytes.clear();
	out.mime.clear();

	if (path.empty())
		return false;

	FormatPtr format = open_input(path, nullptr, nullptr);
	if (!format)
		return false;

	const AVStream *best = nullptr;

	for (unsigned i = 0; i < format->nb_streams; i++) {
		const AVStream *stream = format->streams[i];

		if (!(stream->disposition & AV_DISPOSITION_ATTACHED_PIC) || stream->attached_pic.size <= 0)
			continue;

		/* The front cover wins; otherwise the first picture does. */
		if (!best || (!is_front_cover(best) && is_front_cover(stream)))
			best = stream;
	}

	if (!best)
		return false;

	const AVPacket &picture = best->attached_pic;
	out.bytes.assign(picture.data, picture.data + picture.size);
	out.mime = mime_for(best->codecpar->codec_id);

	return true;
}

std::string find_sidecar_art(const std::string &path)
{
	if (path.empty())
		return std::string();

	const std::string directory = directory_of(path);

	os_dir_t *dir = os_opendir(directory.c_str());
	if (!dir)
		return std::string();

	std::string best;
	int best_rank = -1;

	while (struct os_dirent *entry = os_readdir(dir)) {
		if (entry->directory)
			continue;

		const int rank = sidecar_rank(entry->d_name);
		if (rank < 0)
			continue;

		if (best_rank < 0 || rank < best_rank) {
			best_rank = rank;
			best = directory + "/" + entry->d_name;
		}
	}

	os_closedir(dir);

	return best;
}

} // namespace vr
