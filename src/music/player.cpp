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

#include "music/player.hpp"

#include <algorithm>

namespace vr {

namespace {

/* The next track is opened this long before the current one ends (plus any
 * crossfade), so that it is already buffered when it is needed and tracks run
 * into each other without a gap. */
constexpr int64_t PREFETCH_MS = 5000;

/* Seeking jumps the waveform; a very short fade in hides the click. */
constexpr int64_t SEEK_FADE_MS = 15;

/* Tracks still fading out under a crossfade. Rapid skipping would otherwise
 * stack them up without limit. */
constexpr size_t MAX_OUTGOING = 3;

/* Stands in for a deck the factory could not make, so that failing to open a
 * file goes through exactly the same path as failing to decode one. */
class FailedDeck final : public Deck {
public:
	State state() const override { return State::Failed; }
	size_t read(float *const *, size_t) override { return 0; }
	void seek(int64_t) override {}
	int64_t position_ms() const override { return 0; }
	int64_t duration_ms() const override { return 0; }
	bool seekable() const override { return false; }
	bool produced() const override { return false; }
};

} // namespace

Player::Player(const AudioFormat &format, DeckFactory factory, LogFn log)
	: format_(format),
	  factory_(std::move(factory)),
	  log_(std::move(log))
{
}

Player::~Player() = default;

/* -------------------------------------------------------------------------- */
/* Playlist and settings                                                       */
/* -------------------------------------------------------------------------- */

void Player::set_settings(const PlayerSettings &settings)
{
	if (settings.shuffle != settings_.shuffle) {
		playlist_.set_shuffle(settings.shuffle);
		entries_version_++;
	}

	if (settings.repeat != settings_.repeat)
		entries_version_++;

	settings_ = settings;
}

void Player::set_entries(std::vector<PlaylistEntry> entries)
{
	/* Carry tags already read across, so the dock does not flicker. */
	for (PlaylistEntry &entry : entries) {
		const PlaylistEntry *existing = playlist_.at(playlist_.index_of(entry.path));
		if (existing && existing->tags_loaded) {
			entry.tags = existing->tags;
			entry.tags_loaded = true;
		}
	}

	playlist_.set_entries(std::move(entries));
	playlist_.set_shuffle(settings_.shuffle);
	entries_version_++;
}

void Player::set_tags(const std::string &path, const Tags &tags)
{
	PlaylistEntry *entry = playlist_.at(playlist_.index_of(path));
	if (!entry)
		return;

	entry->tags = tags;
	entry->tags_loaded = true;
	entries_version_++;
}

std::string Player::next_untagged() const
{
	const auto wants_tags = [](const PlaylistEntry *entry) {
		return entry && !entry->tags_loaded && !entry->url;
	};

	/* The playing track and the one after it first, so that what is on
	 * screen fills in straight away. */
	for (int candidate : {playlist_.current(), playlist_.peek_next(settings_.repeat)}) {
		const PlaylistEntry *entry = playlist_.at(candidate);
		if (wants_tags(entry))
			return entry->path;
	}

	for (const PlaylistEntry &entry : playlist_.entries()) {
		if (wants_tags(&entry))
			return entry.path;
	}

	return std::string();
}

/* -------------------------------------------------------------------------- */
/* Transport                                                                   */
/* -------------------------------------------------------------------------- */

void Player::play()
{
	if (transport_ == PlayState::Playing) {
		/* Playing during a fade out to stop cancels it. */
		if (pending_ == Pending::Stop && current_) {
			pending_ = Pending::None;
			current_->gain.fade_to(1.0, ms_frames(settings_.fade_in_ms), false);
		}
		return;
	}

	if (transport_ == PlayState::Paused) {
		transport_ = PlayState::Playing;
		pending_ = Pending::None;

		if (current_) {
			GainEnvelope &gain = current_->gain;

			/* A pause without a fade left the gain at full, so a fade
			 * in has to start from silence. One that was still fading
			 * out turns round from where it got to. */
			if (settings_.fade_in_ms > 0 && !gain.fading() && gain.value() >= 1.0)
				gain.set(0.0);

			gain.fade_to(1.0, ms_frames(settings_.fade_in_ms), false);
			return;
		}

		if (gap_remaining_ > 0)
			return;
	}

	int target = playlist_.current();
	if (target < 0)
		target = playlist_.first();

	if (target >= 0)
		start_entry(target, 0);
}

void Player::pause()
{
	if (transport_ != PlayState::Playing)
		return;

	transport_ = PlayState::Paused;

	if (current_ && settings_.fade_out_ms > 0) {
		current_->gain.fade_to(0.0, ms_frames(settings_.fade_out_ms), false);
		pending_ = Pending::Pause;
	} else {
		pending_ = Pending::None;
	}
}

void Player::toggle_pause()
{
	if (transport_ == PlayState::Playing)
		pause();
	else
		play();
}

void Player::stop()
{
	gap_entry_ = -1;
	gap_remaining_ = 0;

	if (current_ && settings_.fade_out_ms > 0 && transport_ == PlayState::Playing) {
		/* A fade already under way is left to finish rather than being
		 * restarted, or repeated stops would hold it off forever. */
		if (pending_ == Pending::Stop)
			return;

		current_->gain.fade_to(0.0, ms_frames(settings_.fade_out_ms), false);
		pending_ = Pending::Stop;
		prefetch_.reset();
		return;
	}

	stop_now();
}

void Player::next()
{
	advance(manual_crossfade_ms(), true);
}

void Player::previous()
{
	const int target = playlist_.step_back();
	if (target >= 0)
		start_entry(target, manual_crossfade_ms());
}

void Player::restart()
{
	if (!current_) {
		play();
		return;
	}

	transport_ = PlayState::Playing;
	pending_ = Pending::None;

	/* Whatever fade a pause or stop left behind is over; seek() then adds
	 * its own short fade in. */
	current_->gain.set(1.0);
	seek(0);
}

void Player::seek(int64_t ms)
{
	if (!current_ || !current_->deck->seekable())
		return;

	current_->deck->seek(ms);

	/* The track may now have further to run than the crossfade point. */
	crossfade_started_ = false;

	GainEnvelope &gain = current_->gain;
	if (transport_ == PlayState::Playing && pending_ == Pending::None && !gain.fading() && gain.target() >= 1.0) {
		gain.set(0.0);
		gain.fade_to(1.0, ms_frames(SEEK_FADE_MS), false);
	}
}

void Player::play_index(int index)
{
	if (!playlist_.at(index))
		return;

	start_entry(index, manual_crossfade_ms());
}

/* A crossfade asked for by the user, shortened so that it cannot outlast what
 * is left of the track it fades out of. */
int Player::manual_crossfade_ms() const
{
	int crossfade_ms = settings_.crossfade_ms;

	if (crossfade_ms <= 0 || !current_ || transport_ != PlayState::Playing || pending_ != Pending::None)
		return 0;

	const int64_t remaining = current_->deck->duration_ms() - current_->deck->position_ms();
	if (remaining > 0 && remaining < crossfade_ms)
		crossfade_ms = (int)remaining;

	return crossfade_ms;
}

/* -------------------------------------------------------------------------- */
/* Moving between tracks                                                       */
/* -------------------------------------------------------------------------- */

std::unique_ptr<Player::Voice> Player::make_voice(int entry_index)
{
	const PlaylistEntry *entry = playlist_.at(entry_index);
	if (!entry)
		return nullptr;

	auto voice = std::make_unique<Voice>();
	voice->path = entry->path;
	voice->deck = factory_ ? factory_(*entry) : nullptr;

	if (!voice->deck)
		voice->deck = std::make_unique<FailedDeck>();

	return voice;
}

std::unique_ptr<Player::Voice> Player::take_prefetched(int entry_index)
{
	std::unique_ptr<Voice> voice = std::move(prefetch_);
	const PlaylistEntry *entry = playlist_.at(entry_index);

	if (voice && entry && voice->path == entry->path)
		return voice;

	return nullptr;
}

void Player::start_entry(int entry_index, int crossfade_ms)
{
	std::unique_ptr<Voice> voice = take_prefetched(entry_index);
	if (!voice)
		voice = make_voice(entry_index);
	if (!voice)
		return;

	gap_entry_ = -1;
	gap_remaining_ = 0;

	if (crossfade_ms > 0 && current_) {
		current_->gain.fade_to(0.0, ms_frames(crossfade_ms), true);
		outgoing_.push_back(std::move(current_));

		while (outgoing_.size() > MAX_OUTGOING)
			outgoing_.erase(outgoing_.begin());

		voice->gain.set(0.0);
		voice->gain.fade_to(1.0, ms_frames(crossfade_ms), true);
	} else {
		current_.reset();
		outgoing_.clear();

		voice->gain.set(settings_.fade_in_ms > 0 ? 0.0 : 1.0);
		voice->gain.fade_to(1.0, ms_frames(settings_.fade_in_ms), false);
	}

	current_ = std::move(voice);
	note_track_change(entry_index);
}

void Player::note_track_change(int entry_index)
{
	playlist_.set_current(entry_index);
	track_serial_++;
	entries_version_++;
	transport_ = PlayState::Playing;
	pending_ = Pending::None;
	crossfade_started_ = false;
}

void Player::advance(int crossfade_ms, bool user_initiated)
{
	RepeatMode repeat = settings_.repeat;

	/* An explicit skip moves on rather than replaying the track. */
	if (user_initiated && repeat == RepeatMode::One)
		repeat = RepeatMode::All;

	const int target = playlist_.advance(repeat);

	if (target < 0) {
		if (user_initiated)
			stop();
		else
			stop_now();
		return;
	}

	if (crossfade_ms <= 0 && settings_.gap_ms > 0) {
		current_.reset();
		outgoing_.clear();

		gap_entry_ = target;
		gap_remaining_ = ms_frames(settings_.gap_ms);
		transport_ = PlayState::Playing;
		pending_ = Pending::None;
		entries_version_++;
		return;
	}

	start_entry(target, crossfade_ms);
}

/* The current deck has nothing more to give. */
void Player::handle_finished()
{
	const bool produced = current_->deck->produced();
	const Pending pending = pending_;

	if (pending == Pending::Stop) {
		stop_now();
		return;
	}

	/* A deck that never produced a sample counts as a failure, and one that
	 * played clears the count. Without the limit, a playlist of files that
	 * cannot be played would skip round forever. */
	if (produced) {
		consecutive_errors_ = 0;
	} else {
		consecutive_errors_++;

		const bool give_up = !settings_.skip_on_error || consecutive_errors_ > (int)playlist_.size();

		if (log_)
			log_(give_up ? "could not play track, stopping playback"
				     : "could not play track, skipping to the next one");

		if (give_up) {
			stop_now();
			return;
		}
	}

	advance(0, false);

	/* The track ran out during a fade to pause: move on, but stay paused. */
	if (pending == Pending::Pause && transport_ != PlayState::Stopped) {
		transport_ = PlayState::Paused;
		pending_ = Pending::None;
		if (current_)
			current_->gain.set(0.0);
	}
}

void Player::stop_now()
{
	current_.reset();
	outgoing_.clear();
	prefetch_.reset();

	gap_entry_ = -1;
	gap_remaining_ = 0;

	transport_ = PlayState::Stopped;
	pending_ = Pending::None;
	crossfade_started_ = false;
	consecutive_errors_ = 0;
}

void Player::maybe_prefetch()
{
	if (prefetch_ || !current_ || transport_ != PlayState::Playing || pending_ != Pending::None)
		return;

	const int64_t duration = current_->deck->duration_ms();
	if (duration <= 0)
		return;

	const int64_t remaining = duration - current_->deck->position_ms();
	if (remaining > PREFETCH_MS + std::max(0, settings_.crossfade_ms))
		return;

	const int next = playlist_.peek_next(settings_.repeat);
	const PlaylistEntry *entry = playlist_.at(next);

	/* A stream holds a connection open for as long as its deck lives, so
	 * one is only opened when it is actually going to play. */
	if (!entry || entry->url)
		return;

	prefetch_ = make_voice(next);
}

void Player::maybe_crossfade()
{
	if (!current_ || transport_ != PlayState::Playing || pending_ != Pending::None || crossfade_started_ ||
	    settings_.crossfade_ms <= 0)
		return;

	const int64_t duration = current_->deck->duration_ms();
	if (duration <= 0)
		return;

	/* Never fade for longer than half the track, so short jingles still get
	 * to play rather than being crossfaded away as soon as they start. */
	const int64_t effective = std::min<int64_t>(settings_.crossfade_ms, duration / 2);
	if (effective <= 0 || duration - current_->deck->position_ms() > effective)
		return;

	crossfade_started_ = true;

	/* The last track of a playlist that is not repeating plays out in full
	 * rather than fading into nothing. */
	if (playlist_.peek_next(settings_.repeat) < 0)
		return;

	advance((int)effective, false);
}

/* -------------------------------------------------------------------------- */
/* Rendering                                                                   */
/* -------------------------------------------------------------------------- */

bool Player::finished(const Voice &voice)
{
	const Deck::State state = voice.deck->state();
	return state == Deck::State::Ended || state == Deck::State::Failed;
}

size_t Player::mix_voice(Voice &voice, float *const *out, size_t offset, size_t frames)
{
	float *buffers[MAX_MIX_CHANNELS] = {};

	for (uint32_t ch = 0; ch < format_.channels; ch++) {
		if (scratch_[ch].size() < frames)
			scratch_[ch].resize(frames);
		buffers[ch] = scratch_[ch].data();
	}

	const size_t got = voice.deck->read(buffers, frames);

	voice.gain.apply(buffers, format_.channels, got);
	voice.gain.advance(frames - got);

	for (uint32_t ch = 0; ch < format_.channels; ch++) {
		float *dst = out[ch] + offset;
		const float *src = buffers[ch];
		for (size_t i = 0; i < got; i++)
			dst[i] += src[i];
	}

	return got;
}

bool Player::render(float *const *out, size_t frames)
{
	for (uint32_t ch = 0; ch < format_.channels; ch++)
		std::fill(out[ch], out[ch] + frames, 0.0f);

	if (transport_ == PlayState::Stopped)
		return false;

	if (transport_ == PlayState::Paused && pending_ != Pending::Pause)
		return false;

	/* Silence between tracks, kept on the same clock as everything else. */
	if (gap_remaining_ > 0) {
		gap_remaining_ -= std::min<uint64_t>(gap_remaining_, frames);

		if (!gap_remaining_ && gap_entry_ >= 0) {
			const int entry = gap_entry_;
			gap_entry_ = -1;
			start_entry(entry, 0);
		}

		return true;
	}

	maybe_crossfade();
	maybe_prefetch();

	if (current_) {
		const Voice *before = current_.get();
		const size_t got = mix_voice(*current_, out, 0, frames);

		if (finished(*current_)) {
			handle_finished();

			/* With no crossfade or gap the next track picks up at the
			 * very sample this one stopped on. */
			if (current_ && current_.get() != before && transport_ == PlayState::Playing && got < frames)
				mix_voice(*current_, out, got, frames - got);
		}
	}

	for (auto it = outgoing_.begin(); it != outgoing_.end();) {
		mix_voice(**it, out, 0, frames);

		if (finished(**it) || (*it)->gain.silent())
			it = outgoing_.erase(it);
		else
			++it;
	}

	/* A fade to pause or stop has run its course. */
	if (pending_ != Pending::None && (!current_ || !current_->gain.fading())) {
		if (pending_ == Pending::Pause)
			pending_ = Pending::None;
		else
			stop_now();
	}

	return true;
}

/* -------------------------------------------------------------------------- */
/* State                                                                       */
/* -------------------------------------------------------------------------- */

void Player::fill_snapshot(Snapshot &snapshot) const
{
	const PlaylistEntry *entry = playlist_.at(playlist_.current());

	snapshot = Snapshot();
	snapshot.valid = true;
	snapshot.state = transport_;
	snapshot.count = (int)playlist_.size();
	snapshot.index = playlist_.current();
	snapshot.track_serial = track_serial_;

	if (current_) {
		snapshot.elapsed_ms = current_->deck->position_ms();
		snapshot.duration_ms = std::max<int64_t>(0, current_->deck->duration_ms());
	}

	if (entry) {
		snapshot.path = entry->path;
		snapshot.title = entry->display_title();
		snapshot.artist = entry->display_artist();
		snapshot.album = entry->tags.album;
		snapshot.genre = entry->tags.genre;
		snapshot.year = entry->tags.year;
		snapshot.track = entry->tags.track;
		snapshot.seekable = !entry->url && snapshot.duration_ms > 0 && current_ && current_->deck->seekable();
	}

	const PlaylistEntry *next_entry = playlist_.at(playlist_.peek_next(settings_.repeat));
	if (next_entry) {
		snapshot.next_title = next_entry->display_title();
		snapshot.next_artist = next_entry->display_artist();
	}
}

} // namespace vr
