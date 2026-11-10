/*
 * Tests for read_tags, read_embedded_art and find_sidecar_art, run against
 * real files written by FFmpeg's own muxers in each container the plugin is
 * expected to handle.
 */

#include "media-fixtures.hpp"
#include "music/album-art.hpp"
#include "music/loudness-scan.hpp"
#include "music/track-tags.hpp"
#include "test-check.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

using test::check_str;
using test::check_true;

namespace {

const std::map<std::string, std::string> FULL_TAGS = {
	{"title", "Neon Drift"}, {"artist", "Voidscape"}, {"album", "After Hours"},
	{"date", "2024-05-01"},  {"track", "03/12"},      {"genre", "Electronic"},
};

/* Writes `spec` with the full tag set and checks every field comes back. */
void check_container(const char *label, fixture::Spec spec)
{
	if (!fixture::supported(spec)) {
		printf("skip %s (encoder or muxer not in this FFmpeg)\n", label);
		return;
	}

	spec.metadata = FULL_TAGS;
	spec.duration_ms = 200;

	const std::string path = fixture::temp_path(std::string(label) + "." + spec.format);
	std::string error;
	if (!fixture::write(path, spec, &error)) {
		printf("FAIL %s: could not write fixture: %s\n", label, error.c_str());
		test::failures++;
		return;
	}

	vr::Tags tags;
	const bool found = vr::read_tags(path, tags);
	const std::string prefix = std::string(label) + " ";

	check_true((prefix + "found").c_str(), found);
	check_str((prefix + "title").c_str(), tags.title, "Neon Drift");
	check_str((prefix + "artist").c_str(), tags.artist, "Voidscape");
	check_str((prefix + "album").c_str(), tags.album, "After Hours");
	check_str((prefix + "year").c_str(), tags.year, "2024");
	check_str((prefix + "track").c_str(), tags.track, "3");
	check_str((prefix + "genre").c_str(), tags.genre, "Electronic");

	remove(path.c_str());
}

/* Writes `spec` with the given pictures and checks which one is read back. */
void check_art(const char *label, fixture::Spec spec, size_t want_index)
{
	if (!fixture::supported(spec)) {
		printf("skip %s (encoder or muxer not in this FFmpeg)\n", label);
		return;
	}

	spec.duration_ms = 200;

	const std::string path = fixture::temp_path(std::string(label) + "." + spec.format);
	std::string error;
	if (!fixture::write(path, spec, &error)) {
		printf("FAIL %s: could not write fixture: %s\n", label, error.c_str());
		test::failures++;
		return;
	}

	vr::AlbumArtData art;
	const bool found = vr::read_embedded_art(path, art);
	const std::vector<unsigned char> want = fixture::picture_bytes(want_index);
	const std::string prefix = std::string(label) + " ";

	check_true((prefix + "found").c_str(), found);
	check_true((prefix + "right picture").c_str(), art.bytes == std::vector<uint8_t>(want.begin(), want.end()));
	check_str((prefix + "mime").c_str(), art.mime, "image/png");

	remove(path.c_str());
}

fixture::Spec spec(const char *format, const char *codec)
{
	fixture::Spec s;
	s.format = format;
	s.codec = codec;
	return s;
}

} // namespace

int main()
{
	/* Tags in every container. */
	check_container("flac", spec("flac", "flac"));
	check_container("ogg-vorbis", spec("ogg", "libvorbis"));
	check_container("ogg-opus", spec("ogg", "libopus"));
	check_container("m4a", spec("ipod", "aac"));
	check_container("wav", spec("wav", "pcm_s16le"));

	{
		fixture::Spec mp3 = spec("mp3", "libmp3lame");
		mp3.muxer_options = {{"id3v2_version", "4"}};
		check_container("mp3-id3v2.4", mp3);

		mp3.muxer_options = {{"id3v2_version", "3"}};
		check_container("mp3-id3v2.3", mp3);
	}

	/* Non-ASCII text survives (ID3v2.3 stores it as UTF-16). */
	{
		fixture::Spec mp3 = spec("mp3", "libmp3lame");
		if (fixture::supported(mp3)) {
			mp3.muxer_options = {{"id3v2_version", "3"}};
			mp3.metadata = {{"title", "Ünïcode Søñg"}};
			mp3.duration_ms = 200;
			const std::string path = fixture::temp_path("unicode.mp3");
			fixture::write(path, mp3);

			vr::Tags tags;
			vr::read_tags(path, tags);
			check_str("mp3 unicode title", tags.title, "Ünïcode Søñg");
			remove(path.c_str());
		}
	}

	/* ID3v1 only, with its genre stored as a number. FFmpeg's muxer will not
	 * write a bare ID3v1 tag, so it is appended by hand. */
	{
		fixture::Spec mp3 = spec("mp3", "libmp3lame");
		if (fixture::supported(mp3)) {
			mp3.muxer_options = {{"id3v2_version", "0"}};
			mp3.duration_ms = 200;
			const std::string path = fixture::temp_path("id3v1.mp3");
			fixture::write(path, mp3);

			char tag[128] = {};
			const auto put = [&](size_t offset, size_t size, const char *text) {
				strncpy(tag + offset, text, size);
			};
			put(0, 3, "TAG");
			put(3, 30, "Old School");
			put(33, 30, "Tape Deck");
			put(63, 30, "Cassette");
			put(93, 4, "1998");
			tag[126] = 7;  /* ID3v1.1 track number */
			tag[127] = 17; /* genre 17 is Rock */
			std::ofstream(path, std::ios::binary | std::ios::app).write(tag, sizeof(tag));

			vr::Tags tags;
			vr::read_tags(path, tags);
			check_str("id3v1 title", tags.title, "Old School");
			check_str("id3v1 artist", tags.artist, "Tape Deck");
			check_str("id3v1 album", tags.album, "Cassette");
			check_str("id3v1 year", tags.year, "1998");
			check_str("id3v1 track", tags.track, "7");
			check_str("id3v1 genre", tags.genre, "Rock");
			remove(path.c_str());
		}
	}

	/* Files that are not media, or not there. */
	{
		const std::string path = fixture::temp_path("garbage.mp3");
		std::ofstream(path) << "this is not an audio file, whatever its name says";

		vr::Tags tags;
		check_true("garbage: nothing found", !vr::read_tags(path, tags));
		check_str("garbage: no title", tags.title, "");

		vr::AlbumArtData art;
		check_true("garbage: no art", !vr::read_embedded_art(path, art));
		remove(path.c_str());

		check_true("missing file: no tags", !vr::read_tags("/nonexistent/void-radio.flac", tags));
	}

	/* Embedded art: the front cover wins over pictures listed before it. */
	{
		fixture::Spec flac = spec("flac", "flac");
		flac.pictures = {"Cover (back)", "Cover (front)"};
		check_art("flac art: front cover preferred", flac, 1);

		flac.pictures = {"Artist/performer", "Cover (back)"};
		check_art("flac art: first picture otherwise", flac, 0);

		fixture::Spec mp3 = spec("mp3", "libmp3lame");
		mp3.pictures = {"Cover (front)"};
		check_art("mp3 art", mp3, 0);

		fixture::Spec m4a = spec("ipod", "aac");
		m4a.pictures = {"Cover (front)"};
		check_art("m4a art", m4a, 0);

		fixture::Spec wav = spec("wav", "pcm_s16le");
		const std::string path = fixture::temp_path("noart.wav");
		wav.duration_ms = 200;
		fixture::write(path, wav);
		vr::AlbumArtData art;
		check_true("wav: no art", !vr::read_embedded_art(path, art));
		remove(path.c_str());
	}

	/* Sidecar art: the best-named image next to the track. */
	{
		const std::string dir = fixture::temp_path("sidecar");
		mkdir(dir.c_str(), 0755);

		const std::string track = dir + "/song.flac";
		for (const char *name : {"song.flac", "thumb.jpg", "Folder.PNG", "notes.txt"})
			std::ofstream(dir + "/" + name) << "x";

		check_str("sidecar: best name wins", vr::find_sidecar_art(track), dir + "/Folder.PNG");

		for (const char *name : {"song.flac", "thumb.jpg", "Folder.PNG", "notes.txt"})
			remove((dir + "/" + name).c_str());
		rmdir(dir.c_str());
	}

	/* Loudness from ReplayGain and R128 values. */
	{
		check_true("replaygain: -6.54 dB is -11.46 LUFS",
			   std::fabs(vr::loudness_from_replaygain("-6.54 dB") + 11.46f) < 0.001f);
		check_true("replaygain: +2 dB", std::fabs(vr::loudness_from_replaygain(" +2.00 dB") + 20.0f) < 0.001f);
		check_true("replaygain: junk", std::isnan(vr::loudness_from_replaygain("loud")));
		check_true("r128: 0 is -23", std::fabs(vr::loudness_from_r128("0") + 23.0f) < 0.001f);
		check_true("r128: -2560 is -13", std::fabs(vr::loudness_from_r128("-2560") + 13.0f) < 0.001f);
	}

	/* ReplayGain tags in a real file, and the technical details. */
	{
		fixture::Spec spec;
		spec.format = "flac";
		spec.codec = "flac";
		spec.metadata = {{"title", "Gain"},
				 {"REPLAYGAIN_TRACK_GAIN", "-6.54 dB"},
				 {"REPLAYGAIN_TRACK_PEAK", "0.988"},
				 {"REPLAYGAIN_ALBUM_GAIN", "-5.00 dB"}};
		const std::string path = fixture::temp_path("gain.flac");
		check_true("gain: written", fixture::write(path, spec));

		vr::Tags tags;
		check_true("gain: read", vr::read_tags(path, tags));
		check_true("gain: track loudness", std::fabs(tags.track_loudness + 11.46f) < 0.01f);
		check_true("gain: track peak", std::fabs(tags.track_peak - 0.988f) < 0.001f);
		check_true("gain: album loudness", std::fabs(tags.album_loudness + 13.0f) < 0.01f);
		check_str("gain: codec", tags.codec, "FLAC");
		check_str("gain: sample rate", tags.samplerate, "48 kHz");
		remove(path.c_str());
	}

	/* An untagged file is measured, and the result cached against the
	 * file's size and time. */
	{
		fixture::Spec spec;
		spec.format = "flac";
		spec.codec = "flac";
		spec.duration_ms = 3000;
		const std::string path = fixture::temp_path("measure.flac");
		fixture::write(path, spec);

		vr::Tags tags;
		vr::read_tags(path, tags);
		check_true("measure: no loudness in the tags", std::isnan(tags.track_loudness));

		float loudness = 0.0f, peak = 0.0f;
		check_true("measure: measured", vr::measure_loudness(path, loudness, peak));
		check_true("measure: plausible loudness", loudness < 0.0f && loudness > -40.0f);
		check_true("measure: peak found", peak > 0.1f && peak <= 1.01f);

		const std::string cache = fixture::temp_path("loudness-cache.tsv");
		remove(cache.c_str());
		vr::set_loudness_cache_file(cache);

		float got = 0.0f, got_peak = 0.0f;
		check_true("cache: empty at first", !vr::cached_loudness(path, got, got_peak));
		vr::store_loudness(path, loudness, peak);

		/* Read back from disk, not from memory. */
		vr::set_loudness_cache_file(cache);
		check_true("cache: found", vr::cached_loudness(path, got, got_peak));
		check_true("cache: same loudness", std::fabs(got - loudness) < 0.01f);

		/* A changed file is measured again. */
		std::ofstream(path, std::ios::app) << "more";
		check_true("cache: changed file missed", !vr::cached_loudness(path, got, got_peak));

		vr::set_loudness_cache_file("");
		remove(cache.c_str());
		remove(path.c_str());

		check_true("measure: missing file fails", !vr::measure_loudness(path, loudness, peak));
	}

	return test::finish();
}
