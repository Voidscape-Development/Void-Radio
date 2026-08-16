#include "music/playlist.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

static int failures = 0;

static void check(const char *what, long got, long want)
{
	if (got != want) {
		printf("FAIL %-36s got %ld want %ld\n", what, got, want);
		failures++;
	} else {
		printf("ok   %-36s %ld\n", what, got);
	}
}

static void check_true(const char *what, bool ok)
{
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	} else {
		printf("ok   %s\n", what);
	}
}

static vr::Playlist make(int count)
{
	std::vector<vr::PlaylistEntry> entries;
	for (int i = 0; i < count; i++) {
		vr::PlaylistEntry entry;
		entry.path = "/music/track" + std::to_string(i) + ".mp3";
		entries.push_back(entry);
	}

	vr::Playlist list;
	list.set_entries(std::move(entries));
	return list;
}

int main()
{
	/* Sequential playback, stop at the end. */
	{
		vr::Playlist list = make(3);
		check("first", list.first(), 0);
		list.set_current(0);
		check("advance 0>1", list.advance(vr::RepeatMode::Off), 1);
		check("advance 1>2", list.advance(vr::RepeatMode::Off), 2);
		check("advance ends", list.advance(vr::RepeatMode::Off), -1);
	}

	/* Repeat all wraps. */
	{
		vr::Playlist list = make(3);
		list.set_current(2);
		check("repeat all wraps", list.advance(vr::RepeatMode::All), 0);
	}

	/* Repeat one stays put. */
	{
		vr::Playlist list = make(3);
		list.set_current(1);
		check("repeat one", list.advance(vr::RepeatMode::One), 1);
		check("peek repeat one", list.peek_next(vr::RepeatMode::One), 1);
	}

	/* Previous walks back and wraps to the end. */
	{
		vr::Playlist list = make(3);
		list.set_current(1);
		check("step back", list.step_back(), 0);
		check("step back wraps", list.step_back(), 2);
	}

	/* Peek matches what advance will actually pick. */
	{
		vr::Playlist list = make(5);
		list.set_current(0);
		for (int i = 0; i < 8; i++) {
			const int peeked = list.peek_next(vr::RepeatMode::All);
			const int actual = list.advance(vr::RepeatMode::All);
			if (peeked != actual) {
				printf("FAIL peek/advance disagree at step %d: %d vs %d\n", i, peeked, actual);
				failures++;
			}
		}
		printf("ok   peek matches advance over a full loop\n");
	}

	/* Shuffle visits every entry exactly once per pass. */
	{
		vr::Playlist list = make(8);
		list.set_shuffle(true);
		list.set_current(list.first());

		std::set<int> seen;
		seen.insert(list.current());

		for (int i = 0; i < 7; i++)
			seen.insert(list.advance(vr::RepeatMode::All));

		check("shuffle covers every entry", (long)seen.size(), 8);
	}

	/* Wrapping in shuffle mode reshuffles without repeating the last track. */
	{
		bool immediate_repeat = false;

		for (int attempt = 0; attempt < 200; attempt++) {
			vr::Playlist list = make(6);
			list.set_shuffle(true);
			list.set_current(list.first());

			for (int i = 0; i < 5; i++)
				list.advance(vr::RepeatMode::All);

			const int last = list.current();
			if (list.advance(vr::RepeatMode::All) == last)
				immediate_repeat = true;
		}

		check_true("shuffle wrap never replays the same track back to back", !immediate_repeat);
	}

	/* Rebuilding the playlist keeps the current track if it survived. */
	{
		vr::Playlist list = make(4);
		list.set_current(2);
		const std::string playing = list.at(2)->path;

		std::vector<vr::PlaylistEntry> reordered;
		for (int i = 3; i >= 0; i--) {
			vr::PlaylistEntry entry;
			entry.path = "/music/track" + std::to_string(i) + ".mp3";
			reordered.push_back(entry);
		}
		list.set_entries(std::move(reordered));

		check_true("current follows the track across a rebuild", list.at(list.current())->path == playing);
	}

	/* Removing the playing track leaves nothing current rather than dangling. */
	{
		vr::Playlist list = make(3);
		list.set_current(1);

		std::vector<vr::PlaylistEntry> fewer;
		vr::PlaylistEntry entry;
		entry.path = "/music/track0.mp3";
		fewer.push_back(entry);
		list.set_entries(std::move(fewer));

		check("removed current clears index", list.current(), -1);
		check("advance recovers", list.advance(vr::RepeatMode::All), 0);
	}

	/* An empty playlist must not misbehave. */
	{
		vr::Playlist list;
		check("empty first", list.first(), -1);
		check("empty advance", list.advance(vr::RepeatMode::All), -1);
		check("empty step back", list.step_back(), -1);
		check("empty peek", list.peek_next(vr::RepeatMode::All), -1);
		check_true("empty at()", list.at(0) == nullptr && list.at(-1) == nullptr);
	}

	/* Display names fall back sensibly. */
	{
		vr::PlaylistEntry entry;
		entry.path = "/music/Some Folder/07 - Track Name.mp3";
		check_true("title falls back to file stem", entry.display_title() == "07 - Track Name");

		entry.tags.title = "Tagged";
		check_true("tag beats file name", entry.display_title() == "Tagged");

		entry.override_title = "Manual";
		check_true("override beats tag", entry.display_title() == "Manual");
	}

	printf("\n%s (%d failures)\n", failures ? "FAILURES" : "ALL PLAYLIST TESTS PASSED", failures);
	return failures ? 1 : 0;
}
