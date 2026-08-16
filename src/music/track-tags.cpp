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
#include "util/vr-util.hpp"

#include <util/platform.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace vr {

namespace {

/* Tags live near the start of the file for every format handled here, with the
 * exception of MP4 which is walked with seeks instead of a bulk read. */
constexpr size_t MAX_HEADER_READ = 2u * 1024u * 1024u;

struct FileCloser {
	void operator()(FILE *f) const
	{
		if (f)
			fclose(f);
	}
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

uint32_t read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint32_t read_be24(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

uint32_t read_le32(const uint8_t *p)
{
	return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

uint32_t read_syncsafe32(const uint8_t *p)
{
	return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) | ((uint32_t)(p[2] & 0x7F) << 7) |
	       (uint32_t)(p[3] & 0x7F);
}

void assign_if_empty(std::string &field, std::string value)
{
	value = trim(value);
	if (!value.empty() && field.empty())
		field = std::move(value);
}

/* "1/12" and "1" both mean track one. */
std::string normalize_track(const std::string &value)
{
	const size_t slash = value.find('/');
	return slash == std::string::npos ? value : value.substr(0, slash);
}

/* ID3 dates arrive in a handful of shapes; a bare year is all we advertise. */
std::string normalize_year(const std::string &value)
{
	const std::string trimmed = trim(value);

	if (trimmed.size() >= 4) {
		bool numeric = true;
		for (size_t i = 0; i < 4; i++) {
			if (trimmed[i] < '0' || trimmed[i] > '9')
				numeric = false;
		}

		if (numeric)
			return trimmed.substr(0, 4);
	}

	return trimmed;
}

const char *const ID3V1_GENRES[] = {"Blues",
				    "Classic Rock",
				    "Country",
				    "Dance",
				    "Disco",
				    "Funk",
				    "Grunge",
				    "Hip-Hop",
				    "Jazz",
				    "Metal",
				    "New Age",
				    "Oldies",
				    "Other",
				    "Pop",
				    "R&B",
				    "Rap",
				    "Reggae",
				    "Rock",
				    "Techno",
				    "Industrial",
				    "Alternative",
				    "Ska",
				    "Death Metal",
				    "Pranks",
				    "Soundtrack",
				    "Euro-Techno",
				    "Ambient",
				    "Trip-Hop",
				    "Vocal",
				    "Jazz+Funk",
				    "Fusion",
				    "Trance",
				    "Classical",
				    "Instrumental",
				    "Acid",
				    "House",
				    "Game",
				    "Sound Clip",
				    "Gospel",
				    "Noise",
				    "Alt. Rock",
				    "Bass",
				    "Soul",
				    "Punk",
				    "Space",
				    "Meditative",
				    "Instrumental Pop",
				    "Instrumental Rock",
				    "Ethnic",
				    "Gothic",
				    "Darkwave",
				    "Techno-Industrial",
				    "Electronic",
				    "Pop-Folk",
				    "Eurodance",
				    "Dream",
				    "Southern Rock",
				    "Comedy",
				    "Cult",
				    "Gangsta Rap",
				    "Top 40",
				    "Christian Rap",
				    "Pop/Funk",
				    "Jungle",
				    "Native American",
				    "Cabaret",
				    "New Wave",
				    "Psychedelic",
				    "Rave",
				    "Showtunes",
				    "Trailer",
				    "Lo-Fi",
				    "Tribal",
				    "Acid Punk",
				    "Acid Jazz",
				    "Polka",
				    "Retro",
				    "Musical",
				    "Rock & Roll",
				    "Hard Rock",
				    "Folk",
				    "Folk-Rock",
				    "National Folk",
				    "Swing",
				    "Fast-Fusion",
				    "Bebop",
				    "Latin",
				    "Revival",
				    "Celtic",
				    "Bluegrass",
				    "Avantgarde",
				    "Gothic Rock",
				    "Progressive Rock",
				    "Psychedelic Rock",
				    "Symphonic Rock",
				    "Slow Rock",
				    "Big Band",
				    "Chorus",
				    "Easy Listening",
				    "Acoustic",
				    "Humour",
				    "Speech",
				    "Chanson",
				    "Opera",
				    "Chamber Music",
				    "Sonata",
				    "Symphony",
				    "Booty Bass",
				    "Primus",
				    "Porn Groove",
				    "Satire",
				    "Slow Jam",
				    "Club",
				    "Tango",
				    "Samba",
				    "Folklore",
				    "Ballad",
				    "Power Ballad",
				    "Rhythmic Soul",
				    "Freestyle",
				    "Duet",
				    "Punk Rock",
				    "Drum Solo",
				    "A Cappella",
				    "Euro-House",
				    "Dance Hall",
				    "Goa",
				    "Drum & Bass",
				    "Club-House",
				    "Hardcore",
				    "Terror",
				    "Indie",
				    "BritPop",
				    "Negerpunk",
				    "Polsk Punk",
				    "Beat",
				    "Christian Gangsta Rap",
				    "Heavy Metal",
				    "Black Metal",
				    "Crossover",
				    "Contemporary Christian",
				    "Christian Rock",
				    "Merengue",
				    "Salsa",
				    "Thrash Metal",
				    "Anime",
				    "JPop",
				    "Synthpop"};

constexpr size_t ID3V1_GENRE_COUNT = sizeof(ID3V1_GENRES) / sizeof(ID3V1_GENRES[0]);

/* TCON may be a name, "(17)", "17", or "17 Rock" depending on the tagger. */
std::string normalize_genre(const std::string &value)
{
	std::string text = trim(value);

	if (text.size() >= 2 && text.front() == '(') {
		const size_t close = text.find(')');
		if (close != std::string::npos) {
			const std::string inner = text.substr(1, close - 1);
			const std::string rest = trim(text.substr(close + 1));

			if (!rest.empty())
				return rest;

			text = inner;
		}
	}

	bool numeric = !text.empty();
	for (char c : text) {
		if (c < '0' || c > '9')
			numeric = false;
	}

	if (numeric) {
		const long index = strtol(text.c_str(), nullptr, 10);
		if (index >= 0 && (size_t)index < ID3V1_GENRE_COUNT)
			return ID3V1_GENRES[index];
	}

	return text;
}

std::vector<uint8_t> read_head(FILE *file, size_t max_bytes)
{
	std::vector<uint8_t> buffer;

	if (os_fseeki64(file, 0, SEEK_SET) != 0)
		return buffer;

	buffer.resize(max_bytes);
	const size_t read = fread(buffer.data(), 1, max_bytes, file);
	buffer.resize(read);

	return buffer;
}

/* ------------------------------------------------------------------------ */
/* ID3v2                                                                     */
/* ------------------------------------------------------------------------ */

std::string decode_id3_text(const uint8_t *data, size_t size)
{
	if (size == 0)
		return std::string();

	const uint8_t encoding = data[0];
	const uint8_t *text = data + 1;
	const size_t text_size = size - 1;

	switch (encoding) {
	case 0x00:
		return latin1_to_utf8(text, text_size);
	case 0x01:
		return utf16_to_utf8(text, text_size, false);
	case 0x02:
		return utf16_to_utf8(text, text_size, true);
	case 0x03:
	default: {
		size_t length = 0;
		while (length < text_size && text[length] != 0)
			length++;
		return std::string((const char *)text, length);
	}
	}
}

bool parse_id3v2(const std::vector<uint8_t> &buffer, Tags &out)
{
	if (buffer.size() < 10 || memcmp(buffer.data(), "ID3", 3) != 0)
		return false;

	const uint8_t major = buffer[3];
	const uint8_t flags = buffer[5];
	const uint32_t tag_size = read_syncsafe32(&buffer[6]);

	if (major < 2 || major > 4 || tag_size == 0)
		return false;

	size_t available = buffer.size() - 10;
	if (available > tag_size)
		available = tag_size;

	std::vector<uint8_t> body(buffer.begin() + 10, buffer.begin() + 10 + available);

	/* Whole-tag unsynchronisation (ID3v2.3 and earlier). */
	if (flags & 0x80) {
		std::vector<uint8_t> clean;
		clean.reserve(body.size());

		for (size_t i = 0; i < body.size(); i++) {
			clean.push_back(body[i]);
			if (body[i] == 0xFF && i + 1 < body.size() && body[i + 1] == 0x00)
				i++;
		}

		body.swap(clean);
	}

	size_t pos = 0;

	/* Skip the extended header if present. */
	if (flags & 0x40) {
		if (body.size() < 4)
			return false;

		const uint32_t ext_size = major >= 4 ? read_syncsafe32(&body[0]) : read_be32(&body[0]) + 4;
		if (ext_size >= body.size())
			return false;

		pos = ext_size;
	}

	const size_t id_size = major == 2 ? 3 : 4;
	const size_t header_size = major == 2 ? 6 : 10;
	bool found = false;

	while (pos + header_size <= body.size()) {
		char id[5] = {0};
		memcpy(id, &body[pos], id_size);

		if (id[0] == 0)
			break;

		size_t frame_size;
		if (major == 2)
			frame_size = read_be24(&body[pos + 3]);
		else if (major == 4)
			frame_size = read_syncsafe32(&body[pos + 4]);
		else
			frame_size = read_be32(&body[pos + 4]);

		const size_t data_pos = pos + header_size;
		if (frame_size == 0 || data_pos + frame_size > body.size())
			break;

		const uint8_t *frame = &body[data_pos];
		const std::string text = decode_id3_text(frame, frame_size);

		if (!text.empty()) {
			const bool v2 = major == 2;

			if (!strcmp(id, v2 ? "TT2" : "TIT2"))
				assign_if_empty(out.title, text);
			else if (!strcmp(id, v2 ? "TP1" : "TPE1"))
				assign_if_empty(out.artist, text);
			else if (!strcmp(id, v2 ? "TAL" : "TALB"))
				assign_if_empty(out.album, text);
			else if (!strcmp(id, v2 ? "TCO" : "TCON"))
				assign_if_empty(out.genre, normalize_genre(text));
			else if (!strcmp(id, v2 ? "TRK" : "TRCK"))
				assign_if_empty(out.track, normalize_track(text));
			else if (!strcmp(id, v2 ? "TYE" : "TYER") || !strcmp(id, "TDRC") || !strcmp(id, "TDRL"))
				assign_if_empty(out.year, normalize_year(text));
			else
				goto next_frame;

			found = true;
		}

	next_frame:
		pos = data_pos + frame_size;
	}

	return found;
}

bool parse_id3v1(FILE *file, Tags &out)
{
	if (os_fseeki64(file, -128, SEEK_END) != 0)
		return false;

	uint8_t tag[128];
	if (fread(tag, 1, sizeof(tag), file) != sizeof(tag))
		return false;

	if (memcmp(tag, "TAG", 3) != 0)
		return false;

	assign_if_empty(out.title, latin1_to_utf8(tag + 3, 30));
	assign_if_empty(out.artist, latin1_to_utf8(tag + 33, 30));
	assign_if_empty(out.album, latin1_to_utf8(tag + 63, 30));
	assign_if_empty(out.year, latin1_to_utf8(tag + 93, 4));

	/* ID3v1.1 stores the track number in the last two comment bytes. */
	if (tag[125] == 0 && tag[126] != 0)
		assign_if_empty(out.track, std::to_string((int)tag[126]));

	if (tag[127] < ID3V1_GENRE_COUNT)
		assign_if_empty(out.genre, ID3V1_GENRES[tag[127]]);

	return true;
}

/* ------------------------------------------------------------------------ */
/* Vorbis comments (FLAC, Ogg Vorbis, Opus)                                  */
/* ------------------------------------------------------------------------ */

bool parse_vorbis_comment(const uint8_t *data, size_t size, Tags &out)
{
	size_t pos = 0;

	if (size < 8)
		return false;

	const uint32_t vendor_len = read_le32(&data[pos]);
	pos += 4;

	if (vendor_len > size - pos)
		return false;
	pos += vendor_len;

	if (pos + 4 > size)
		return false;

	const uint32_t count = read_le32(&data[pos]);
	pos += 4;

	bool found = false;

	for (uint32_t i = 0; i < count; i++) {
		if (pos + 4 > size)
			break;

		const uint32_t length = read_le32(&data[pos]);
		pos += 4;

		if (length > size - pos)
			break;

		const std::string entry((const char *)&data[pos], length);
		pos += length;

		const size_t equals = entry.find('=');
		if (equals == std::string::npos)
			continue;

		const std::string key = to_lower(entry.substr(0, equals));
		const std::string value = entry.substr(equals + 1);

		if (key == "title")
			assign_if_empty(out.title, value);
		else if (key == "artist" || key == "performer")
			assign_if_empty(out.artist, value);
		else if (key == "album")
			assign_if_empty(out.album, value);
		else if (key == "genre")
			assign_if_empty(out.genre, value);
		else if (key == "date" || key == "year")
			assign_if_empty(out.year, normalize_year(value));
		else if (key == "tracknumber")
			assign_if_empty(out.track, normalize_track(value));
		else
			continue;

		found = true;
	}

	return found;
}

bool parse_flac(const std::vector<uint8_t> &buffer, Tags &out)
{
	if (buffer.size() < 8 || memcmp(buffer.data(), "fLaC", 4) != 0)
		return false;

	size_t pos = 4;

	while (pos + 4 <= buffer.size()) {
		const uint8_t header = buffer[pos];
		const bool last = (header & 0x80) != 0;
		const uint8_t type = header & 0x7F;
		const uint32_t length = read_be24(&buffer[pos + 1]);

		pos += 4;

		if (length > buffer.size() - pos)
			break;

		if (type == 4 && parse_vorbis_comment(&buffer[pos], length, out))
			return true;

		if (last)
			break;

		pos += length;
	}

	return false;
}

/* Reassembles Ogg packets from the page structure so that comment blocks
 * spanning several pages (common once cover art is embedded) still parse. */
bool parse_ogg(const std::vector<uint8_t> &buffer, Tags &out)
{
	if (buffer.size() < 27 || memcmp(buffer.data(), "OggS", 4) != 0)
		return false;

	size_t pos = 0;
	int packet_index = 0;
	std::vector<uint8_t> packet;

	while (pos + 27 <= buffer.size()) {
		if (memcmp(&buffer[pos], "OggS", 4) != 0)
			break;

		const uint8_t segment_count = buffer[pos + 26];
		const size_t table_pos = pos + 27;

		if (table_pos + segment_count > buffer.size())
			break;

		size_t data_pos = table_pos + segment_count;

		for (uint8_t i = 0; i < segment_count; i++) {
			const uint8_t length = buffer[table_pos + i];

			if (data_pos + length > buffer.size())
				return false;

			packet.insert(packet.end(), buffer.begin() + data_pos, buffer.begin() + data_pos + length);
			data_pos += length;

			if (length == 255)
				continue;

			/* Packet complete. The comment header is packet index 1. */
			if (packet_index == 1) {
				if (packet.size() > 7 && packet[0] == 3 && memcmp(&packet[1], "vorbis", 6) == 0)
					return parse_vorbis_comment(&packet[7], packet.size() - 7, out);
				if (packet.size() > 8 && memcmp(packet.data(), "OpusTags", 8) == 0)
					return parse_vorbis_comment(&packet[8], packet.size() - 8, out);
				if (packet.size() > 9 && memcmp(packet.data(),
								"\x7f"
								"FLAC",
								5) == 0)
					return false;
				return false;
			}

			packet.clear();
			packet_index++;
		}

		pos = data_pos;
	}

	return false;
}

/* ------------------------------------------------------------------------ */
/* MP4 / M4A                                                                 */
/* ------------------------------------------------------------------------ */

struct Mp4Box {
	char type[5];
	int64_t content_offset;
	int64_t content_size;
};

bool read_mp4_box(FILE *file, int64_t offset, int64_t limit, Mp4Box &box)
{
	if (offset + 8 > limit)
		return false;

	if (os_fseeki64(file, offset, SEEK_SET) != 0)
		return false;

	uint8_t header[16];
	if (fread(header, 1, 8, file) != 8)
		return false;

	int64_t size = (int64_t)read_be32(header);
	memcpy(box.type, header + 4, 4);
	box.type[4] = 0;
	int64_t header_size = 8;

	if (size == 1) {
		if (fread(header + 8, 1, 8, file) != 8)
			return false;

		size = ((int64_t)read_be32(header + 8) << 32) | (int64_t)read_be32(header + 12);
		header_size = 16;
	} else if (size == 0) {
		size = limit - offset;
	}

	if (size < header_size || offset + size > limit)
		return false;

	box.content_offset = offset + header_size;
	box.content_size = size - header_size;

	return true;
}

bool find_mp4_box(FILE *file, int64_t start, int64_t limit, const char *type, Mp4Box &found)
{
	int64_t offset = start;

	while (offset < limit) {
		Mp4Box box;
		if (!read_mp4_box(file, offset, limit, box))
			return false;

		if (memcmp(box.type, type, 4) == 0) {
			found = box;
			return true;
		}

		offset = box.content_offset + box.content_size;
	}

	return false;
}

std::string read_ilst_value(FILE *file, const Mp4Box &item, bool numeric_pair)
{
	Mp4Box data;
	if (!find_mp4_box(file, item.content_offset, item.content_offset + item.content_size, "data", data))
		return std::string();

	if (data.content_size <= 8 || data.content_size > (1 << 20))
		return std::string();

	if (os_fseeki64(file, data.content_offset, SEEK_SET) != 0)
		return std::string();

	std::vector<uint8_t> payload((size_t)data.content_size);
	if (fread(payload.data(), 1, payload.size(), file) != payload.size())
		return std::string();

	/* 4 bytes version+flags, 4 bytes locale, then the value. */
	const uint8_t *value = payload.data() + 8;
	const size_t value_size = payload.size() - 8;

	if (numeric_pair) {
		if (value_size >= 4)
			return std::to_string((int)((value[2] << 8) | value[3]));
		return std::string();
	}

	return std::string((const char *)value, value_size);
}

bool parse_mp4(FILE *file, int64_t file_size, Tags &out)
{
	Mp4Box ftyp;
	if (!read_mp4_box(file, 0, file_size, ftyp))
		return false;

	if (memcmp(ftyp.type, "ftyp", 4) != 0)
		return false;

	Mp4Box moov;
	if (!find_mp4_box(file, 0, file_size, "moov", moov))
		return false;

	Mp4Box udta;
	if (!find_mp4_box(file, moov.content_offset, moov.content_offset + moov.content_size, "udta", udta))
		return false;

	Mp4Box meta;
	if (!find_mp4_box(file, udta.content_offset, udta.content_offset + udta.content_size, "meta", meta))
		return false;

	/* `meta` is a full box: skip its version and flags before its children. */
	Mp4Box ilst;
	if (!find_mp4_box(file, meta.content_offset + 4, meta.content_offset + meta.content_size, "ilst", ilst))
		return false;

	const int64_t limit = ilst.content_offset + ilst.content_size;
	int64_t offset = ilst.content_offset;
	bool found = false;

	while (offset < limit) {
		Mp4Box item;
		if (!read_mp4_box(file, offset, limit, item))
			break;

		const char *type = item.type;

		if (!memcmp(type,
			    "\xa9"
			    "nam",
			    4))
			assign_if_empty(out.title, read_ilst_value(file, item, false));
		else if (!memcmp(type,
				 "\xa9"
				 "ART",
				 4) ||
			 !memcmp(type, "aART", 4))
			assign_if_empty(out.artist, read_ilst_value(file, item, false));
		else if (!memcmp(type,
				 "\xa9"
				 "alb",
				 4))
			assign_if_empty(out.album, read_ilst_value(file, item, false));
		else if (!memcmp(type,
				 "\xa9"
				 "gen",
				 4) ||
			 !memcmp(type, "gnre", 4))
			assign_if_empty(out.genre, normalize_genre(read_ilst_value(file, item, false)));
		else if (!memcmp(type,
				 "\xa9"
				 "day",
				 4))
			assign_if_empty(out.year, normalize_year(read_ilst_value(file, item, false)));
		else if (!memcmp(type, "trkn", 4))
			assign_if_empty(out.track, read_ilst_value(file, item, true));
		else {
			offset = item.content_offset + item.content_size;
			continue;
		}

		found = true;
		offset = item.content_offset + item.content_size;
	}

	return found;
}

/* ------------------------------------------------------------------------ */
/* RIFF INFO (WAV)                                                           */
/* ------------------------------------------------------------------------ */

bool parse_riff(const std::vector<uint8_t> &buffer, Tags &out)
{
	if (buffer.size() < 12 || memcmp(buffer.data(), "RIFF", 4) != 0 || memcmp(&buffer[8], "WAVE", 4) != 0)
		return false;

	size_t pos = 12;
	bool found = false;

	while (pos + 8 <= buffer.size()) {
		const uint32_t chunk_size = read_le32(&buffer[pos + 4]);
		const size_t content = pos + 8;

		if (chunk_size > buffer.size() - content)
			break;

		if (memcmp(&buffer[pos], "LIST", 4) == 0 && chunk_size >= 4 &&
		    memcmp(&buffer[content], "INFO", 4) == 0) {
			size_t sub = content + 4;
			const size_t end = content + chunk_size;

			while (sub + 8 <= end) {
				const uint32_t sub_size = read_le32(&buffer[sub + 4]);
				const size_t sub_content = sub + 8;

				if (sub_size > end - sub_content)
					break;

				const std::string value = latin1_to_utf8(&buffer[sub_content], sub_size);

				if (!memcmp(&buffer[sub], "INAM", 4))
					assign_if_empty(out.title, value);
				else if (!memcmp(&buffer[sub], "IART", 4))
					assign_if_empty(out.artist, value);
				else if (!memcmp(&buffer[sub], "IPRD", 4))
					assign_if_empty(out.album, value);
				else if (!memcmp(&buffer[sub], "IGNR", 4))
					assign_if_empty(out.genre, value);
				else if (!memcmp(&buffer[sub], "ICRD", 4))
					assign_if_empty(out.year, normalize_year(value));
				else if (!memcmp(&buffer[sub], "ITRK", 4) || !memcmp(&buffer[sub], "IPRT", 4))
					assign_if_empty(out.track, normalize_track(value));
				else {
					sub = sub_content + sub_size + (sub_size & 1);
					continue;
				}

				found = true;
				sub = sub_content + sub_size + (sub_size & 1);
			}
		}

		pos = content + chunk_size + (chunk_size & 1);
	}

	return found;
}

} // namespace

bool read_tags(const std::string &path, Tags &out)
{
	if (path.empty() || is_url(path))
		return false;

	const int64_t file_size = os_get_file_size(path.c_str());
	if (file_size <= 0)
		return false;

	FilePtr file(os_fopen(path.c_str(), "rb"));
	if (!file)
		return false;

	const size_t head_size = (size_t)((file_size < (int64_t)MAX_HEADER_READ) ? file_size : MAX_HEADER_READ);
	const std::vector<uint8_t> head = read_head(file.get(), head_size);

	bool found = false;

	if (head.size() >= 4) {
		if (!memcmp(head.data(), "ID3", 3))
			found = parse_id3v2(head, out);
		else if (!memcmp(head.data(), "fLaC", 4))
			found = parse_flac(head, out);
		else if (!memcmp(head.data(), "OggS", 4))
			found = parse_ogg(head, out);
		else if (!memcmp(head.data(), "RIFF", 4))
			found = parse_riff(head, out);
		else
			found = parse_mp4(file.get(), file_size, out);
	}

	/* Fall back to ID3 tags, which taggers happily bolt onto other formats. */
	if (out.title.empty() || out.artist.empty()) {
		if (!found && head.size() >= 10 && !memcmp(head.data(), "ID3", 3))
			found |= parse_id3v2(head, out);

		found |= parse_id3v1(file.get(), out);
	}

	return found && !out.empty();
}

} // namespace vr
