#pragma once

/*
 * Writes real media files for the tests with FFmpeg's own muxers and encoders,
 * so the decoder and tag readers are exercised on what a user would actually
 * have rather than on hand-built byte blobs.
 *
 * The audio is a sawtooth that counts samples: sample n of every channel holds
 * (n % SAW_PERIOD) - SAW_OFFSET as a 16-bit value. With a lossless codec at the
 * output rate, a decoded sample therefore says exactly where in the file it
 * came from.
 */

#include <map>
#include <string>

namespace fixture {

constexpr int SAW_PERIOD = 20000;
constexpr int SAW_OFFSET = 10000;

struct Spec {
	std::string format; /* muxer name: "flac", "wav", "ipod", "ogg", "mp3" */
	std::string codec;  /* encoder name: "flac", "pcm_s16le", "aac", "libvorbis", "libmp3lame" */
	int sample_rate = 48000;
	int channels = 2;
	int duration_ms = 2000;
	std::map<std::string, std::string> metadata;
	bool cover = false;
	std::string cover_comment = "Cover (front)";
};

/* Whether this FFmpeg build can write the spec at all (encoders such as
 * libmp3lame are optional). */
bool supported(const Spec &spec);

bool write(const std::string &path, const Spec &spec, std::string *error = nullptr);

/* The bytes written as cover art: a 1x1 PNG. */
const unsigned char *cover_bytes(size_t &size);

/* Decoded sample value -> position in the sawtooth. */
int saw_position(float sample);

std::string temp_path(const std::string &name);

} // namespace fixture
