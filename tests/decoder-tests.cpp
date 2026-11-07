/*
 * Tests for AudioDecoder and StreamDeck against real files written by FFmpeg.
 *
 * The fixtures carry a sawtooth that counts samples (see media-fixtures.hpp),
 * so with a lossless codec at the output rate every decoded sample says where
 * it came from, and seeks can be checked to the sample.
 */

#include "audio/audio-decoder.hpp"
#include "audio/stream-deck.hpp"
#include "media-fixtures.hpp"
#include "test-check.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

using test::check;
using test::check_true;

namespace {

constexpr uint32_t RATE = 48000;
const vr::AudioFormat FORMAT{RATE, 2, 0};

std::string make(const std::string &name, fixture::Spec spec)
{
	const std::string path = fixture::temp_path(name);
	std::string error;
	if (!fixture::write(path, spec, &error)) {
		printf("could not write fixture %s: %s\n", name.c_str(), error.c_str());
		test::failures++;
	}
	return path;
}

/* Decodes everything, checking the sawtooth is unbroken. */
void decode_all(const char *label, vr::AudioDecoder &decoder, int64_t expect_frames, bool exact_values)
{
	std::vector<float> planes[vr::MAX_MIX_CHANNELS];
	int64_t total = 0;
	bool contiguous = true;
	bool values_ok = true;
	vr::AudioDecoder::Status status;

	for (;;) {
		for (auto &plane : planes)
			plane.clear();

		size_t frames = 0;
		int64_t start = 0;
		status = decoder.decode(planes, frames, start);
		if (status != vr::AudioDecoder::Status::Ok)
			break;

		if (start != total)
			contiguous = false;

		if (exact_values) {
			for (size_t i = 0; i < frames; i++) {
				const int want = (int)((total + (int64_t)i) % fixture::SAW_PERIOD);
				if (fixture::saw_position(planes[0][i]) != want ||
				    fixture::saw_position(planes[1][i]) != want)
					values_ok = false;
			}
		}

		total += (int64_t)frames;
	}

	std::string what = std::string(label) + ": ends cleanly";
	check_true(what.c_str(), status == vr::AudioDecoder::Status::EndOfStream);
	what = std::string(label) + ": frame count";
	check(what.c_str(), total, expect_frames);
	what = std::string(label) + ": positions contiguous";
	check_true(what.c_str(), contiguous);
	if (exact_values) {
		what = std::string(label) + ": every sample in order";
		check_true(what.c_str(), values_ok);
	}
}

/* Waits for a deck to produce, reading `ms` worth of audio. */
int64_t read_for(vr::StreamDeck &deck, int64_t ms, std::vector<float> *left = nullptr)
{
	float a[480], b[480];
	float *out[2] = {a, b};
	int64_t got_total = 0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

	while (got_total < (int64_t)vr::ms_to_frames(ms, RATE) && std::chrono::steady_clock::now() < deadline) {
		const vr::Deck::State state = deck.state();
		if (state == vr::Deck::State::Ended || state == vr::Deck::State::Failed)
			break;

		const size_t got = deck.read(out, 480);
		if (left)
			left->insert(left->end(), a, a + got);
		got_total += (int64_t)got;

		if (!got)
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}

	return got_total;
}

vr::Deck::State wait_state(vr::StreamDeck &deck, vr::Deck::State want)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (deck.state() != want && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	return deck.state();
}

} // namespace

int main()
{
	fixture::Spec flac;
	flac.format = "flac";
	flac.codec = "flac";
	flac.cover = true;
	const std::string flac_path = make("cover.flac", flac);

	/* Whole file, sample exact, cover art ignored. */
	{
		vr::AudioDecoder decoder;
		std::string error;
		check_true("flac: opens", decoder.open(flac_path, false, FORMAT, nullptr, &error));
		check("flac: duration", decoder.duration_ms(), 2000);
		check_true("flac: seekable", decoder.seekable());
		decode_all("flac", decoder, 2 * RATE, true);
	}

	/* Seeks land on the exact sample asked for. */
	{
		vr::AudioDecoder decoder;
		decoder.open(flac_path, false, FORMAT, nullptr);

		for (int64_t ms : {1234, 250, 1999, 0}) {
			check_true("seek: accepted", decoder.seek(ms));

			std::vector<float> planes[vr::MAX_MIX_CHANNELS];
			size_t frames = 0;
			int64_t start = 0;
			decoder.decode(planes, frames, start);

			const int64_t want = ms * RATE / 1000;
			check("seek: start frame", start, want);
			check("seek: first sample", fixture::saw_position(planes[0][0]), want % fixture::SAW_PERIOD);
		}
	}

	/* A different source rate is resampled to the output rate. */
	{
		fixture::Spec wav;
		wav.format = "wav";
		wav.codec = "pcm_s16le";
		wav.sample_rate = 44100;
		wav.channels = 1;
		const std::string path = make("44k.wav", wav);

		vr::AudioDecoder decoder;
		check_true("wav 44.1k mono: opens", decoder.open(path, false, FORMAT, nullptr));
		std::vector<float> planes[vr::MAX_MIX_CHANNELS];
		int64_t total = 0;
		size_t frames;
		int64_t start;
		while (decoder.decode(planes, frames, start) == vr::AudioDecoder::Status::Ok)
			total += (int64_t)frames;
		check_true("wav 44.1k mono: resampled length", total > 2 * RATE - 100 && total < 2 * RATE + 100);
		check_true("wav 44.1k mono: both channels filled", planes[1].size() == planes[0].size());
		remove(path.c_str());
	}

	/* Files that cannot be played. */
	{
		vr::AudioDecoder decoder;
		std::string error;
		check_true("missing file: fails",
			   !decoder.open("/nonexistent/void-radio.flac", false, FORMAT, nullptr, &error));
		check_true("missing file: says why", !error.empty());

		vr::StreamDeck deck("/nonexistent/void-radio.flac", false, FORMAT);
		check_true("missing file: deck fails",
			   wait_state(deck, vr::Deck::State::Failed) == vr::Deck::State::Failed);
		check_true("missing file: produced nothing", !deck.produced());
	}

	/*
	 * The original bug: a file with cover art, seeked part way through,
	 * must carry on from the new position rather than ending.
	 */
	const auto seek_regression = [](const char *label, const std::string &path, bool exact) {
		vr::StreamDeck deck(path, false, FORMAT);
		read_for(deck, 300);

		deck.seek(1200);
		std::string what = std::string(label) + ": position held during seek";
		check(what.c_str(), deck.position_ms(), 1200);

		std::vector<float> left;
		const int64_t got = read_for(deck, 300, &left);
		what = std::string(label) + ": still playing after seek";
		check_true(what.c_str(), deck.state() == vr::Deck::State::Ready && got > 0);
		what = std::string(label) + ": position after seek";
		check(what.c_str(), deck.position_ms(), 1500);

		if (exact && !left.empty()) {
			what = std::string(label) + ": first sample after seek";
			check(what.c_str(), fixture::saw_position(left[0]), (1200 * 48) % fixture::SAW_PERIOD);
		}

		read_for(deck, 5000);
		what = std::string(label) + ": ends at the real end";
		check_true(what.c_str(), wait_state(deck, vr::Deck::State::Ended) == vr::Deck::State::Ended);
		what = std::string(label) + ": end position";
		check_true(what.c_str(), deck.position_ms() >= 1950 && deck.position_ms() <= 2050);
	};

	seek_regression("flac with cover", flac_path, true);

	fixture::Spec mp3;
	mp3.format = "mp3";
	mp3.codec = "libmp3lame";
	mp3.cover = true;
	if (fixture::supported(mp3)) {
		const std::string path = make("cover.mp3", mp3);
		seek_regression("mp3 with cover", path, false);
		remove(path.c_str());
	} else {
		printf("skip mp3 with cover (no libmp3lame)\n");
	}

	remove(flac_path.c_str());
	return test::finish();
}
