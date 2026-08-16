#include "util/text-template.hpp"

#include <cstdio>
#include <string>

static int failures = 0;

static void check(const char *what, const std::string &got, const std::string &want)
{
	if (got != want) {
		printf("FAIL %-34s got '%s' want '%s'\n", what, got.c_str(), want.c_str());
		failures++;
	} else {
		printf("ok   %-34s '%s'\n", what, got.c_str());
	}
}

int main()
{
	vr::Snapshot snapshot;
	snapshot.valid = true;
	snapshot.state = vr::PlayState::Playing;
	snapshot.title = "Neon Drift";
	snapshot.artist = "Voidscape";
	snapshot.album = "After Hours";
	snapshot.path = "/music/set/03 neon drift.flac";
	snapshot.next_title = "Afterglow";
	snapshot.elapsed_ms = 65000;
	snapshot.duration_ms = 245000;
	snapshot.progress = 65000.0f / 245000.0f;
	snapshot.index = 2;
	snapshot.count = 12;

	vr::TemplateContext ctx;
	ctx.snapshot = &snapshot;
	ctx.have_snapshot = true;

	const auto run = [&](const std::string &format) {
		return vr::expand_template(format, ctx);
	};

	check("plain fields", run("{title} - {artist}"), "Neon Drift - Voidscape");
	check("times", run("{elapsed} / {duration}"), "1:05 / 4:05");
	check("remaining", run("-{remaining}"), "-3:00");
	check("percent", run("{percent}"), "27%");
	check("position", run("Track {index} of {count}"), "Track 3 of 12");
	check("filename", run("{filename}"), "03 neon drift.flac");
	check("state", run("{state}"), "Playing");
	check("escaped brackets", run("\\[{state}\\]"), "[Playing]");
	check("escaped brace", run("\\{title\\}"), "{title}");
	check("case insensitive", run("{TITLE}"), "Neon Drift");
	check("unknown field kept", run("{bogus}"), "{bogus}");
	check("optional group kept", run("{title}[ - {artist}]"), "Neon Drift - Voidscape");
	check("newlines", run("{title}\n{artist}"), "Neon Drift\nVoidscape");
	check("next up", run("Next: {next_title}"), "Next: Afterglow");
	check("unterminated brace", run("{title} {oops"), "Neon Drift {oops");
	check("literal text only", run("Now playing"), "Now playing");

	/* Empty fields collapse their optional group. */
	snapshot.artist.clear();
	check("optional group dropped", run("{title}[ - {artist}]"), "Neon Drift");
	check("empty field alone", run("{artist}"), "");
	check("two groups", run("[{artist} - ]{title}[ ({album})]"), "Neon Drift (After Hours)");

	/* A live stream has no duration. */
	snapshot.duration_ms = 0;
	snapshot.progress = 0.0f;
	check("stream duration empty", run("{elapsed}[ / {duration}]"), "1:05");
	check("stream percent empty", run("[{percent}]"), "");

	/* Nothing playing at all. */
	vr::Snapshot idle;
	vr::TemplateContext idle_ctx;
	idle_ctx.snapshot = &idle;
	idle_ctx.have_snapshot = false;
	check("idle template", vr::expand_template("[{title} - ]Off air", idle_ctx), "Off air");
	check("null snapshot", vr::expand_template("{title}", vr::TemplateContext{}), "");

	printf("\n%s (%d failures)\n", failures ? "FAILURES" : "ALL TEMPLATE TESTS PASSED", failures);
	return failures ? 1 : 0;
}
