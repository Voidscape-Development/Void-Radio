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

#include "util/text-template.hpp"

namespace vr {

namespace {

/* Substitutes one placeholder. Returns false when the field exists but is
 * empty, which is what lets optional [...] groups disappear. */
bool lookup_field(const TemplateContext &context, const std::string &name, std::string &out)
{
	const Snapshot &snap = *context.snapshot;

	out.clear();

	if (name == "title")
		out = snap.title;
	else if (name == "artist")
		out = snap.artist;
	else if (name == "album")
		out = snap.album;
	else if (name == "year")
		out = snap.year;
	else if (name == "genre")
		out = snap.genre;
	else if (name == "track")
		out = snap.track;
	else if (name == "path")
		out = snap.path;
	else if (name == "filename")
		out = file_name(snap.path);
	else if (name == "next_title")
		out = snap.next_title;
	else if (name == "next_artist")
		out = snap.next_artist;
	else if (name == "elapsed")
		out = format_time(snap.elapsed_ms, context.time_format);
	else if (name == "duration")
		out = snap.duration_ms > 0 ? format_time(snap.duration_ms, context.time_format) : std::string();
	else if (name == "remaining")
		out = snap.duration_ms > 0 ? format_time(snap.duration_ms - snap.elapsed_ms, context.time_format)
					   : std::string();
	else if (name == "percent")
		out = snap.duration_ms > 0 ? std::to_string((int)(snap.progress * 100.0f + 0.5f)) + "%" : std::string();
	else if (name == "index")
		out = snap.index >= 0 ? std::to_string(snap.index + 1) : std::string();
	else if (name == "count")
		out = snap.count > 0 ? std::to_string(snap.count) : std::string();
	else if (name == "state") {
		if (!context.have_snapshot)
			out.clear();
		else if (snap.state == PlayState::Playing)
			out = context.state_playing;
		else if (snap.state == PlayState::Paused)
			out = context.state_paused;
		else
			out = context.state_stopped;
	} else {
		/* Unknown placeholder: leave it in place so the mistake is
		 * visible rather than silently swallowing text. */
		out = "{" + name + "}";
		return true;
	}

	return !out.empty();
}

/*
 * Expands a template.
 *
 * {field}  is replaced by the field's value.
 * [ ... ]  is dropped entirely if any field inside it is empty, so
 *          "{title}[ - {artist}]" loses the dash on an untagged track.
 */
std::string expand_impl(const std::string &format, const TemplateContext &context)
{
	std::string out;
	out.reserve(format.size() + 32);

	std::string group;
	bool in_group = false;
	bool group_complete = true;

	const auto emit = [&](const std::string &text) {
		if (in_group)
			group += text;
		else
			out += text;
	};

	for (size_t i = 0; i < format.size(); i++) {
		const char c = format[i];

		/* A backslash escapes the next character, which is how a literal
		 * bracket or brace gets into the output. */
		if (c == '\\' && i + 1 < format.size()) {
			const char next = format[i + 1];

			if (next == '[' || next == ']' || next == '{' || next == '}' || next == '\\') {
				emit(std::string(1, next));
				i++;
				continue;
			}
		}

		if (c == '[' && !in_group) {
			in_group = true;
			group_complete = true;
			group.clear();
			continue;
		}

		if (c == ']' && in_group) {
			in_group = false;
			if (group_complete)
				out += group;
			group.clear();
			continue;
		}

		if (c == '{') {
			const size_t close = format.find('}', i + 1);
			if (close == std::string::npos) {
				emit(std::string(1, c));
				continue;
			}

			const std::string name = to_lower(trim(format.substr(i + 1, close - i - 1)));
			std::string value;

			if (!lookup_field(context, name, value))
				group_complete = false;

			emit(value);
			i = close;
			continue;
		}

		emit(std::string(1, c));
	}

	/* An unterminated group still renders, minus the empty-field rule. */
	if (in_group && group_complete)
		out += group;

	return out;
}

} // namespace

std::string expand_template(const std::string &format, const TemplateContext &context)
{
	static const Snapshot empty;

	TemplateContext resolved = context;
	if (!resolved.snapshot)
		resolved.snapshot = &empty;

	return expand_impl(format, resolved);
}

} // namespace vr
