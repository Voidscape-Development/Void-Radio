# Void Radio

Music playback for OBS Studio: a playlist source that focuses purely on audio, a progress bar
source that tracks it, and a filter that feeds now-playing text into any text source.

## What's in it

### Music Source

A playlist driven audio source, in the spirit of the VLC playlist source but audio only and
built around music playback.

* Add individual files, whole folders (optionally including subfolders), and network URLs.
* Shuffle, repeat all, repeat one, or stop at the end of the list.
* Crossfade between tracks, or a plain silent gap when crossfade is off. Crossfading applies to
  natural track ends and to manual skips alike, and is clamped so a short track cannot be
  swallowed by the fade.
* Independent fade in and fade out for starting, pausing and stopping.
* Song titles and artists are read from the file's own tags. Anything missing can be filled in
  by hand per track from the dock, and those edits are saved with the scene collection.
* `Keep playing when hidden` (on by default) keeps the music going when you cut to a scene that
  does not contain the source. Turn it off to have playback pause whenever the source is not on
  the program output.
* Global hotkeys for play/pause, stop, next, previous and restart.
* Appears in OBS's own media controls, so the built-in transport buttons work too.

Audio is decoded by private OBS media sources and mixed back out through the Music Source
itself, so the music arrives in the audio mixer as a single channel with working volume,
filters, monitoring and track routing.

### Music Progress Bar

A video source that shows how far along the selected Music Source is.

* Width, height, corner radius, and an optional border.
* Background and progress layers are independently a solid colour, a linear gradient, an image
  file, or the rendered output of another source in the scene.
* Fills left to right, right to left, bottom to top, top to bottom, or from the centre out.
* Movement is interpolated between decoder updates, so the fill glides rather than stepping once
  per audio packet.
* On a track change the bar animates: it fills to the end and sweeps back to the start
  (configurable duration), eases back from wherever it was, or jumps instantly.
* Choose what it does when nothing is playing, or when the source is a live stream with no fixed
  length: sit empty, hide, or run a looping sweep.

### Music Info

A filter you add to a text source. It reads from a Music Source and keeps the text updated.

Write a template using any of these fields:

| | | | |
|---|---|---|---|
| `{title}` | `{artist}` | `{album}` | `{year}` |
| `{genre}` | `{track}` | `{filename}` | `{path}` |
| `{elapsed}` | `{duration}` | `{remaining}` | `{percent}` |
| `{state}` | `{index}` | `{count}` | `{next_title}` |
| `{next_artist}` | | | |

Text wrapped in square brackets disappears when a field inside it is empty, so
`{title}[ - {artist}]` loses the dash on an untagged track. Use `\[`, `\]`, `\{`, `\}` for
literal brackets and braces. A separate template can be set for when playback is stopped.

Elapsed and total times can be formatted as `m:ss`, `mm:ss`, `h:mm:ss`, `hh:mm:ss`, or
automatically (adding the hours field only once a track passes an hour).

### Now Playing dock

Under **View → Docks → Now Playing**: pick a Music Source, see the current title and artist,
scrub with the seek bar, and use the transport, shuffle and repeat controls. The playlist below
can be reordered by dragging, played by double-clicking, and each track has a context menu for
editing its title and artist or removing it from the list.

## Building

Standard OBS plugin template build; see the
[plugin template wiki](https://github.com/obsproject/obs-plugintemplate/wiki) for the full
details.

| Platform | Toolchain |
|---|---|
| Windows | Visual Studio 17 2022, CMake 3.30.5 |
| macOS | Xcode 16.0, CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3, `ninja-build`, `pkg-config`, `build-essential` |

```sh
cmake --preset ubuntu-x86_64   # or windows-x64 / macos
cmake --build --preset ubuntu-x86_64
```

The plugin needs Qt 6 and `obs-frontend-api` for the dock; both are on by default and provided
by the template's dependency setup.

## Tests

The parts that do not need OBS — the tag readers, the template expander and playlist navigation
— build and run on their own:

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

## Notes

* Tags are read without a third-party tagging library: ID3v2.2/2.3/2.4 and ID3v1, Vorbis
  comments in FLAC and Ogg (Vorbis, Opus), iTunes-style metadata in MP4/M4A, and RIFF INFO in
  WAV. Anything unreadable falls back to the file name, and can be overridden by hand.
* Decoding itself is whatever the OBS media source supports, which is far wider than that list.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
