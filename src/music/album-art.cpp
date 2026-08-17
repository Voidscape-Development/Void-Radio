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
#include "util/vr-util.hpp"

#include <util/platform.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace vr {

namespace {

/* Artwork is routinely a couple of megabytes; anything past this is assumed to
 * be a corrupt length field rather than a genuine cover. */
constexpr size_t MAX_ART_BYTES = 32u * 1024u * 1024u;

/* How much of an Ogg stream to sift through looking for the comment header.
 * The header is the second packet, so this is generous even for files with a
 * large picture attached. */
constexpr size_t MAX_OGG_SCAN = 16u * 1024u * 1024u;

/* ID3v2 picture type 3 is the front cover, which is the one worth showing. */
constexpr uint8_t PICTURE_TYPE_FRONT = 3;

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

bool read_at(FILE *file, int64_t offset, void *dest, size_t size)
{
	if (os_fseeki64(file, offset, SEEK_SET) != 0)
		return false;

	return fread(dest, 1, size, file) == size;
}

bool read_block(FILE *file, int64_t offset, size_t size, std::vector<uint8_t> &out)
{
	if (size == 0 || size > MAX_ART_BYTES)
		return false;

	out.resize(size);

	if (!read_at(file, offset, out.data(), size)) {
		out.clear();
		return false;
	}

	return true;
}

/*
 * A picture found while walking a file. Several may turn up; the front cover
 * wins, and failing that the first one seen.
 */
struct Candidate {
	AlbumArtData art;
	uint8_t type = 0xFF;
	bool have = false;
};

bool better_candidate(const Candidate &current, uint8_t type)
{
	if (!current.have)
		return true;

	/* Once a front cover is in hand nothing displaces it. */
	if (current.type == PICTURE_TYPE_FRONT)
		return false;

	return type == PICTURE_TYPE_FRONT;
}

void offer(Candidate &current, uint8_t type, std::vector<uint8_t> bytes, std::string mime)
{
	if (bytes.empty() || !better_candidate(current, type))
		return;

	current.art.bytes = std::move(bytes);
	current.art.mime = std::move(mime);
	current.type = type;
	current.have = true;
}

/* ------------------------------------------------------------------------ */
/* base64                                                                    */
/* ------------------------------------------------------------------------ */

int base64_value(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

std::vector<uint8_t> base64_decode(const char *data, size_t size)
{
	std::vector<uint8_t> out;
	out.reserve(size / 4 * 3);

	uint32_t accumulator = 0;
	int bits = 0;

	for (size_t i = 0; i < size; i++) {
		const int value = base64_value(data[i]);

		/* Whitespace and padding are skipped; anything else stops the
		 * decode rather than producing garbage. */
		if (value < 0) {
			if (data[i] == '=' || data[i] == '\n' || data[i] == '\r' || data[i] == ' ' || data[i] == '\t')
				continue;
			break;
		}

		accumulator = (accumulator << 6) | (uint32_t)value;
		bits += 6;

		if (bits >= 8) {
			bits -= 8;
			out.push_back((uint8_t)((accumulator >> bits) & 0xFF));
		}

		if (out.size() > MAX_ART_BYTES)
			return std::vector<uint8_t>();
	}

	return out;
}

/* ------------------------------------------------------------------------ */
/* FLAC PICTURE payload, used by FLAC itself and by base64 Vorbis comments    */
/* ------------------------------------------------------------------------ */

bool parse_picture_block(const uint8_t *data, size_t size, Candidate &best)
{
	size_t pos = 0;

	const auto need = [&](size_t bytes) {
		return pos + bytes <= size;
	};

	if (!need(4))
		return false;

	const uint32_t type = read_be32(&data[pos]);
	pos += 4;

	if (!need(4))
		return false;

	const uint32_t mime_length = read_be32(&data[pos]);
	pos += 4;

	if (mime_length > size - pos)
		return false;

	const std::string mime((const char *)&data[pos], mime_length);
	pos += mime_length;

	if (!need(4))
		return false;

	const uint32_t desc_length = read_be32(&data[pos]);
	pos += 4;

	if (desc_length > size - pos)
		return false;

	pos += desc_length;

	/* Width, height, colour depth and indexed colour count. */
	if (!need(16))
		return false;

	pos += 16;

	if (!need(4))
		return false;

	const uint32_t data_length = read_be32(&data[pos]);
	pos += 4;

	if (data_length == 0 || data_length > size - pos || data_length > MAX_ART_BYTES)
		return false;

	/* A "-->" mime type means the payload is a URL, not an image. */
	if (mime == "-->")
		return false;

	offer(best, (uint8_t)std::min<uint32_t>(type, 0xFE), std::vector<uint8_t>(&data[pos], &data[pos] + data_length),
	      mime);

	return true;
}

/* ------------------------------------------------------------------------ */
/* Vorbis comments (FLAC, Ogg Vorbis, Opus)                                  */
/* ------------------------------------------------------------------------ */

void parse_vorbis_pictures(const uint8_t *data, size_t size, Candidate &best)
{
	size_t pos = 0;

	if (size < 8)
		return;

	const uint32_t vendor_length = read_le32(&data[pos]);
	pos += 4;

	if (vendor_length > size - pos)
		return;

	pos += vendor_length;

	if (pos + 4 > size)
		return;

	const uint32_t count = read_le32(&data[pos]);
	pos += 4;

	/* Legacy COVERART entries carry the mime type in a separate field, so
	 * they are held back until the whole comment block has been read. */
	std::vector<uint8_t> legacy;
	std::string legacy_mime;

	for (uint32_t i = 0; i < count; i++) {
		if (pos + 4 > size)
			break;

		const uint32_t length = read_le32(&data[pos]);
		pos += 4;

		if (length > size - pos)
			break;

		const char *entry = (const char *)&data[pos];
		const size_t entry_size = length;
		pos += length;

		const void *equals = memchr(entry, '=', entry_size);
		if (!equals)
			continue;

		const size_t key_size = (const char *)equals - entry;
		const std::string key = to_lower(std::string(entry, key_size));
		const char *value = entry + key_size + 1;
		const size_t value_size = entry_size - key_size - 1;

		if (key == "metadata_block_picture") {
			const std::vector<uint8_t> decoded = base64_decode(value, value_size);
			if (!decoded.empty())
				parse_picture_block(decoded.data(), decoded.size(), best);
		} else if (key == "coverart" && legacy.empty()) {
			legacy = base64_decode(value, value_size);
		} else if (key == "coverartmime") {
			legacy_mime.assign(value, value_size);
		}
	}

	/* Only used when the file carried no proper picture block. */
	if (!legacy.empty() && !best.have)
		offer(best, PICTURE_TYPE_FRONT, std::move(legacy), legacy_mime);
}

/* ------------------------------------------------------------------------ */
/* ID3v2 (MP3, and taggers that bolt ID3 onto other containers)              */
/* ------------------------------------------------------------------------ */

/* Skips a tag description terminated by one or two nulls, depending on the
 * text encoding byte that preceded it. Returns the offset of the image data,
 * or `size` when the frame is malformed. */
size_t skip_description(const uint8_t *data, size_t size, size_t pos, uint8_t encoding)
{
	const bool wide = encoding == 1 || encoding == 2;

	if (wide) {
		while (pos + 1 < size) {
			if (data[pos] == 0 && data[pos + 1] == 0)
				return pos + 2;
			pos += 2;
		}

		return size;
	}

	while (pos < size) {
		if (data[pos] == 0)
			return pos + 1;
		pos++;
	}

	return size;
}

void parse_apic(const uint8_t *data, size_t size, bool v22, Candidate &best)
{
	if (size < 4)
		return;

	size_t pos = 0;
	const uint8_t encoding = data[pos++];

	std::string mime;

	if (v22) {
		/* Three character format code: "JPG", "PNG". */
		const std::string format = to_lower(std::string((const char *)&data[pos], 3));
		pos += 3;

		if (format == "png")
			mime = "image/png";
		else if (format == "jpg")
			mime = "image/jpeg";
		else
			mime = "image/" + format;
	} else {
		const size_t start = pos;
		while (pos < size && data[pos] != 0)
			pos++;

		mime.assign((const char *)&data[start], pos - start);

		if (pos < size)
			pos++; /* the terminator */
	}

	if (pos >= size)
		return;

	const uint8_t type = data[pos++];

	pos = skip_description(data, size, pos, encoding);
	if (pos >= size)
		return;

	if (mime == "-->")
		return;

	offer(best, type, std::vector<uint8_t>(&data[pos], &data[size]), mime);
}

void parse_id3v2_pictures(FILE *file, int64_t offset, Candidate &best)
{
	uint8_t header[10];
	if (!read_at(file, offset, header, sizeof(header)))
		return;

	if (memcmp(header, "ID3", 3) != 0)
		return;

	const uint8_t major = header[3];
	const uint8_t tag_flags = header[5];
	const uint32_t tag_size = read_syncsafe32(&header[6]);

	if (major < 2 || major > 4 || tag_size == 0 || tag_size > MAX_ART_BYTES)
		return;

	std::vector<uint8_t> body;
	if (!read_block(file, offset + 10, tag_size, body))
		return;

	/* Whole-tag unsynchronisation (ID3v2.3 and earlier). */
	if (tag_flags & 0x80) {
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

	if (tag_flags & 0x40) {
		if (body.size() < 4)
			return;

		const uint32_t ext_size = major >= 4 ? read_syncsafe32(&body[0]) : read_be32(&body[0]) + 4;
		if (ext_size >= body.size())
			return;

		pos = ext_size;
	}

	const size_t id_size = major == 2 ? 3 : 4;
	const size_t header_size = major == 2 ? 6 : 10;

	while (pos + header_size <= body.size()) {
		char id[5] = {0};
		memcpy(id, &body[pos], id_size);

		if (id[0] == 0)
			break;

		size_t frame_size;
		uint16_t frame_flags = 0;

		if (major == 2) {
			frame_size = read_be24(&body[pos + 3]);
		} else {
			frame_size = major == 4 ? read_syncsafe32(&body[pos + 4]) : read_be32(&body[pos + 4]);
			frame_flags = (uint16_t)((body[pos + 8] << 8) | body[pos + 9]);
		}

		size_t data_pos = pos + header_size;
		if (frame_size == 0 || data_pos + frame_size > body.size())
			break;

		const bool picture = major == 2 ? !strcmp(id, "PIC") : !strcmp(id, "APIC");

		if (picture) {
			std::vector<uint8_t> frame(&body[data_pos], &body[data_pos] + frame_size);

			/* ID3v2.4 frames may carry a four byte data length
			 * indicator, and may be unsynchronised individually. */
			if (major == 4) {
				if ((frame_flags & 0x0001) && frame.size() >= 4)
					frame.erase(frame.begin(), frame.begin() + 4);

				if (frame_flags & 0x0002) {
					std::vector<uint8_t> clean;
					clean.reserve(frame.size());

					for (size_t i = 0; i < frame.size(); i++) {
						clean.push_back(frame[i]);
						if (frame[i] == 0xFF && i + 1 < frame.size() && frame[i + 1] == 0x00)
							i++;
					}

					frame.swap(clean);
				}
			}

			if (!frame.empty())
				parse_apic(frame.data(), frame.size(), major == 2, best);

			if (best.have && best.type == PICTURE_TYPE_FRONT)
				return;
		}

		pos = data_pos + frame_size;
	}
}

/* ------------------------------------------------------------------------ */
/* FLAC                                                                      */
/* ------------------------------------------------------------------------ */

void parse_flac_pictures(FILE *file, int64_t file_size, Candidate &best)
{
	int64_t pos = 4; /* past "fLaC" */

	while (pos + 4 <= file_size) {
		uint8_t header[4];
		if (!read_at(file, pos, header, sizeof(header)))
			return;

		const bool last = (header[0] & 0x80) != 0;
		const uint8_t type = header[0] & 0x7F;
		const uint32_t length = read_be24(&header[1]);

		pos += 4;

		if ((int64_t)length > file_size - pos)
			return;

		if (type == 6 || type == 4) {
			std::vector<uint8_t> block;

			if (read_block(file, pos, length, block)) {
				if (type == 6)
					parse_picture_block(block.data(), block.size(), best);
				else
					parse_vorbis_pictures(block.data(), block.size(), best);
			}

			if (best.have && best.type == PICTURE_TYPE_FRONT)
				return;
		}

		if (last)
			return;

		pos += length;
	}
}

/* ------------------------------------------------------------------------ */
/* Ogg (Vorbis, Opus)                                                        */
/* ------------------------------------------------------------------------ */

/* Reassembles Ogg packets from the page structure. The comment header, which
 * is where artwork lives, is packet index 1. */
void parse_ogg_pictures(FILE *file, int64_t file_size, Candidate &best)
{
	const size_t scan = (size_t)std::min<int64_t>(file_size, (int64_t)MAX_OGG_SCAN);

	std::vector<uint8_t> buffer;
	if (!read_block(file, 0, scan, buffer))
		return;

	if (buffer.size() < 27 || memcmp(buffer.data(), "OggS", 4) != 0)
		return;

	size_t pos = 0;
	int packet_index = 0;
	std::vector<uint8_t> packet;

	while (pos + 27 <= buffer.size()) {
		if (memcmp(&buffer[pos], "OggS", 4) != 0)
			return;

		const uint8_t segment_count = buffer[pos + 26];
		const size_t table_pos = pos + 27;

		if (table_pos + segment_count > buffer.size())
			return;

		size_t data_pos = table_pos + segment_count;

		for (uint8_t i = 0; i < segment_count; i++) {
			const uint8_t length = buffer[table_pos + i];

			if (data_pos + length > buffer.size())
				return;

			packet.insert(packet.end(), buffer.begin() + data_pos, buffer.begin() + data_pos + length);
			data_pos += length;

			if (length == 255)
				continue;

			if (packet_index == 1) {
				if (packet.size() > 7 && packet[0] == 3 && memcmp(&packet[1], "vorbis", 6) == 0)
					parse_vorbis_pictures(&packet[7], packet.size() - 7, best);
				else if (packet.size() > 8 && memcmp(packet.data(), "OpusTags", 8) == 0)
					parse_vorbis_pictures(&packet[8], packet.size() - 8, best);

				return;
			}

			packet.clear();
			packet_index++;
		}

		pos = data_pos;
	}
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

	uint8_t header[16];
	if (!read_at(file, offset, header, 8))
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

void parse_mp4_pictures(FILE *file, int64_t file_size, Candidate &best)
{
	Mp4Box ftyp;
	if (!read_mp4_box(file, 0, file_size, ftyp) || memcmp(ftyp.type, "ftyp", 4) != 0)
		return;

	Mp4Box moov;
	if (!find_mp4_box(file, 0, file_size, "moov", moov))
		return;

	Mp4Box udta;
	if (!find_mp4_box(file, moov.content_offset, moov.content_offset + moov.content_size, "udta", udta))
		return;

	Mp4Box meta;
	if (!find_mp4_box(file, udta.content_offset, udta.content_offset + udta.content_size, "meta", meta))
		return;

	/* `meta` is a full box: skip its version and flags before its children. */
	Mp4Box ilst;
	if (!find_mp4_box(file, meta.content_offset + 4, meta.content_offset + meta.content_size, "ilst", ilst))
		return;

	Mp4Box covr;
	if (!find_mp4_box(file, ilst.content_offset, ilst.content_offset + ilst.content_size, "covr", covr))
		return;

	Mp4Box data;
	if (!find_mp4_box(file, covr.content_offset, covr.content_offset + covr.content_size, "data", data))
		return;

	if (data.content_size <= 8)
		return;

	uint8_t prefix[8];
	if (!read_at(file, data.content_offset, prefix, sizeof(prefix)))
		return;

	/* The low byte of version+flags names the image format. */
	const uint8_t format = prefix[3];
	const char *mime = format == 14 ? "image/png" : (format == 13 ? "image/jpeg" : "");

	std::vector<uint8_t> bytes;
	if (!read_block(file, data.content_offset + 8, (size_t)(data.content_size - 8), bytes))
		return;

	offer(best, PICTURE_TYPE_FRONT, std::move(bytes), mime);
}

/* ------------------------------------------------------------------------ */
/* Sidecar artwork                                                           */
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

bool read_embedded_art(const std::string &path, AlbumArtData &out)
{
	out.bytes.clear();
	out.mime.clear();

	if (path.empty() || is_url(path))
		return false;

	const int64_t file_size = os_get_file_size(path.c_str());
	if (file_size <= 12)
		return false;

	FilePtr file(os_fopen(path.c_str(), "rb"));
	if (!file)
		return false;

	uint8_t magic[4];
	if (!read_at(file.get(), 0, magic, sizeof(magic)))
		return false;

	Candidate best;

	if (!memcmp(magic, "ID3", 3))
		parse_id3v2_pictures(file.get(), 0, best);
	else if (!memcmp(magic, "fLaC", 4))
		parse_flac_pictures(file.get(), file_size, best);
	else if (!memcmp(magic, "OggS", 4))
		parse_ogg_pictures(file.get(), file_size, best);
	else if (memcmp(magic, "RIFF", 4) != 0)
		parse_mp4_pictures(file.get(), file_size, best);

	/* Taggers happily bolt an ID3v2 tag onto containers that have their own
	 * metadata, so it is worth a second look when nothing turned up. */
	if (!best.have && memcmp(magic, "ID3", 3) != 0)
		parse_id3v2_pictures(file.get(), 0, best);

	if (!best.have)
		return false;

	out = std::move(best.art);

	return !out.bytes.empty();
}

std::string find_sidecar_art(const std::string &path)
{
	if (path.empty() || is_url(path))
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
