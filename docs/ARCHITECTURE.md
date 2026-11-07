# How Void Radio is put together

This is the map for working on the plugin: what lives where, which thread owns what, and where
to go to change a given behaviour. The README covers what the plugin does for users.

## The shape of it

```
                 OBS callbacks, hotkeys, dock, other sources
                        |                         ^
                 post commands               read published
                        v                    snapshot / entries
  +---------------------------------------------------------------+
  | MusicSource (src/music/music-source*.cpp)  - the OBS side      |
  |                                                                |
  |   engine thread: every 10 ms                                   |
  |     1. run posted commands  --->  Player                       |
  |     2. Player::render()     --->  one block of audio           |
  |     3. obs_source_output_audio, timestamped by sample count    |
  |     4. publish snapshot + entry list                           |
  |                                                                |
  |   Player (src/music/player.cpp)  - transport logic, no threads |
  |     playlist, fades, crossfades, gaps, prefetch, error skips   |
  |        |  reads from, through the Deck interface               |
  |        v                                                       |
  |   StreamDeck (src/audio/stream-deck.cpp)  - one per track      |
  |     decode thread -> buffer ~0.5 s ahead -> read() never waits |
  |        |                                                       |
  |        v                                                       |
  |   AudioDecoder (src/audio/audio-decoder.cpp)  - FFmpeg         |
  |                                                                |
  |   LibraryWorker (src/music/library-worker.cpp)                 |
  |     folder scans + tag reads, results posted back as commands  |
  +---------------------------------------------------------------+
```

Everything that shows music - the Music Widget, the progress bar, the info filter and the dock -
reads the published `Snapshot` through `src/music/music-api.hpp`. None of them know how playback
works, which is what made it possible to replace the engine without touching them.

## Threads, and who owns what

The rule that keeps this simple: **every piece of mutable state has exactly one owning thread.**
Other threads hand it work or read a published copy; they never reach in.

| Thread | Owns | Talks to others by |
|---|---|---|
| Engine (`MusicSource::engine_thread`) | the `Player`, every deck handle, the output timeline | draining `CommandQueue`; publishing `Snapshot` and entries under short locks |
| Deck decode thread (one per `StreamDeck`) | its `AudioDecoder` | a small mutex-guarded buffer the engine reads from |
| Library worker | folder walking, tag reading | posting `set_entries` / `set_tags` commands |
| Art loader (widget) | cover decoding and blurring | a single request/result slot the widget polls |
| OBS UI / graphics / hotkey threads | nothing of ours | posting commands, reading snapshots |

So when you add a transport feature, it goes in `Player` and is reached by posting a lambda from
`MusicSource`. You never need a new lock for it.

## Directory map

| Path | What it is |
|---|---|
| `src/audio/` | Decoding, independent of OBS. `AudioDecoder` wraps FFmpeg; `StreamDeck` runs one on a thread; `Deck` is the interface the player uses; `ffmpeg-util` holds the RAII handles and `open_input`. |
| `src/music/player.*` | The transport state machine. Pure logic on a sample clock; tested by `tests/player-tests.cpp`. |
| `src/music/gain-envelope.*` | Sample-accurate fades (linear, or equal-power for crossfades). |
| `src/music/playlist.*` | Entry list, play order, shuffle and repeat navigation. |
| `src/music/music-source.*` | The OBS source: engine thread, output, publishing, transport entry points. |
| `src/music/music-source-settings.cpp` | Reading settings into the engine, and the dock's edits written back. |
| `src/music/music-source-info.cpp` | `obs_source_info`, properties and defaults. |
| `src/music/source-hotkeys.*` | Per-source hotkeys. |
| `src/music/library-worker.*` | Folder scanning and tag reading off the engine thread. |
| `src/music/music-api.*`, `music-link.cpp` | The public face other sources use: snapshot, entries, transport calls, and `MusicLink` for binding to a music source by name and UUID. |
| `src/music/track-tags.*`, `album-art.*` | Tags and embedded/sidecar cover art via FFmpeg. |
| `src/sources/music-widget-source.cpp`, `src/sources/music-widget/` | The Music Widget. `widget.hpp` lists its element types and the few functions its files share. |
| `src/sources/progress-bar-source.cpp`, `src/render/bar-renderer.*` | The progress bar, also drawn inside the widget. |
| `src/filters/music-info-filter.*` | Writes now-playing text into a text source. |
| `src/ui/now-playing-dock.*` | The Qt dock. |
| `src/render/` | Text rasterising, image decoding and blurring, shared drawing. |
| `src/util/` | Small helpers: time formatting, text templates, paths. |

## Where to change things

- **How tracks follow each other** (crossfade, gap, repeat, what happens on an error):
  `Player::advance`, `Player::handle_finished`, `Player::maybe_crossfade`. Add a case to
  `tests/player-tests.cpp`; fake decks let you script durations, failures, slow opens and slow
  seeks.
- **Fades** on play, pause and stop: `Player::play`, `pause`, `stop`, and the end of
  `Player::render`, which finishes a pending pause or stop once its fade is done.
- **A new transport action**: add it to `Player`, add a posting wrapper to `MusicSource`, then
  expose it through `music-api.hpp` and, if wanted, `source-hotkeys.cpp`.
- **A new source setting**: default and property in `music-source-info.cpp`, read it in
  `music-source-settings.cpp` (into `PlayerSettings` if the player needs it).
- **Decoding, seeking, streams**: `AudioDecoder` (formats, seek accuracy, resampling) and
  `StreamDeck` (buffering, reconnecting). `tests/decoder-tests.cpp` writes real files with
  FFmpeg and checks every sample.
- **What a track is called**: `track-tags.cpp` for reading, `PlaylistEntry::display_title` for
  the fallbacks.
- **Anything the widget draws**: `src/sources/music-widget/`; `widget-render.cpp` for drawing,
  `widget-settings.cpp` and `widget-properties.cpp` for its options.

## Things that are easy to get wrong

- **Never block the engine thread.** It produces audio in real time. Anything that touches the
  disk or the network belongs on a deck thread or the library worker.
- **Output timestamps come from the sample count**, not the clock: the start of a run of audio
  plus samples sent. Do not stamp blocks with `os_gettime_ns()` - jittery timestamps make OBS
  resync, which is audible.
- **A deck that is still opening or seeking is not finished.** `Deck::state()` only reports
  `Ended` or `Failed` when that is true; short reads in between are normal and the player outputs
  silence to keep the timeline continuous.
- **Readers see published state, which can be up to one block old.** That is why
  `MusicSource::seek` also updates the published position straight away: a scrubber reading back
  its own seek must not jump.
- **Cover art never reaches the decoder.** `AudioDecoder` discards every stream but the chosen
  audio one. OBS's own media source does not, which is what used to break seeking.

## Tests

| Test | Covers | Needs |
|---|---|---|
| `player` | transport behaviour end to end, with fake decks | nothing |
| `decoder` | decoding, sample-exact seeks, the cover-art seek case, bad files | FFmpeg dev packages |
| `tag` | tags in every container, cover art choice, sidecar art | FFmpeg dev packages |
| `playlist`, `template` | play order; the now-playing template language | nothing |
| `obs-smoke` | the built plugin inside a real headless libobs | libobs, an X display |

`README.md` has the commands. CI runs everything except `obs-smoke` on every push and pull
request.

## Style

- `clang-format` 19 for C++ under `src/` and `gersemi` for CMake, both checked in CI. The
  settings are in `.clang-format` and `.gersemirc`.
- Comments say why, not what. When a line exists because of an OBS or FFmpeg quirk, the comment
  names the quirk, so the next person does not "simplify" it away.
