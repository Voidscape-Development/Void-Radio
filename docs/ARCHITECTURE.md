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

## Volume, ducking and silence

Three things change the music's level on its way out, each where it can be tested on its own:

- **Volume levelling** lives in `Player`. Each `Voice` carries a level and a target; `level_for()`
  works the target out from the entry's loudness (ReplayGain or R128 tags, read in
  `track-tags.cpp`, or measured) and `apply_level()` settles towards it smoothly, so loudness
  that arrives mid-track is a ramp, not a jump. Measuring is `LoudnessMeter`
  (`src/audio/loudness.cpp`, BS.1770 K-weighting and gating, pure) driven over a whole file by
  `measure_loudness()` on the library worker, with results kept in a small tab-separated cache in
  the plugin's config folder, keyed by path, size and modification time. The worker hands a
  file's tags over before measuring it (marked `measuring`) and again afterwards, so names show
  at once; `next_untagged()` asks for each file to be measured once.
- **Silence skipping** also lives in `Player`, in `mix_voice()`: `trim_lead()` reads on through
  the deck's buffer until sound starts, so seconds of silence are skipped within a block or two,
  and `watch_tail()` ends a voice early once the last stretch of a track has stayed quiet. Seeks
  turn both off for that voice.
- **Ducking** is applied by `MusicSource` to the whole output block, after the player, because it
  depends on another OBS source. The trigger is bound by name with an audio capture callback that
  keeps only the block's peak in an atomic; the engine takes it once per block and `Ducker`
  (`src/music/ducker.cpp`, pure) turns it into a gain moving at a steady rate in decibels.

## The queue

The queue lives in `Player`, as paths rather than indices, so reordering or reshuffling the
playlist does not disturb it and a track that leaves the playlist leaves the queue
(`set_entries()`). `take_following()` is the one place that decides what plays next: repeat-one
first, then the queue, then the playlist, which resumes from `resume_path_`, the track that was
playing when the queue took over. `peek_following()` answers the same question without moving,
for prefetching, crossfades, the untagged queue and `next_title`. The dock and obs-websocket
address the queue by playlist index; `MusicSource` turns that into a path on the engine thread.

## Music Reactive

`src/filters/music-reactive-filter.cpp` taps the linked music source with an audio capture
callback, like the widget's visualiser, and hands each tick's audio to `BeatDetector`
(`src/audio/beat-detector.cpp`, pure). That splits the sound into bass and treble with two
biquads, follows each band's level in 10 ms steps, and calls a beat when the bass rises sharply
against the last few steps and tops the last hit as it dies away. The filter turns the chosen
signal into one 0..1 drive, then draws its target through `music-reactive.effect` (brightness,
tint, opacity) inside a transform for the size pulse and shake.

## Remote control

`src/integrations/websocket.cpp` registers vendor requests with obs-websocket through libobs's
proc handlers, the same calls obs-websocket's own API header makes, so the plugin needs nothing
from obs-websocket to build and skips this when it is not installed. Requests use the public
`music-api.hpp` functions. Events are sent from each music source's `publish()` on its engine
thread, only when the track, its title or artist, or the play state actually changed.

## The Music Widget

The widget's settings come in two layers. What the user mostly touches is the design: a layout,
a style, and up to six lines with roles. What the renderer reads is the detailed per-element
settings it has always read: each element's position, size, font and colours.

`resolve_settings()` in `src/sources/music-widget/widget-design.cpp` turns the first into the
second. It copies the settings, runs the layout engine (`widget-layout.cpp`, pure arithmetic) to
place the art, lines and bar for the widget's current size, and writes the chosen style's
colours, fonts and shapes into each element. The renderer reads that copy exactly as if the user
had set those values by hand, so it has no idea layouts or styles exist. Free placement and the
Custom style skip the corresponding step, which is how widgets saved before this existed keep
working unchanged; `migrate_settings()` puts them on both when they load.

The layout editor (`src/ui/widget-editor.cpp`) works on the same layers:

- `element_boxes()` resolves the settings and reports where each element is drawn, which is
  what the editor outlines.
- `detach_to_free_placement()` writes those resolved boxes into each element's own settings and
  switches to free placement, so the first drag under an automatic layout moves nothing else.
- `place_element()` stores a dragged box relative to the element's anchor.
- `set_stacking()` rewrites the layer numbers when the layers list is reordered.

Four elements are never arranged by a layout: the visualiser (`viz_`), the logo image (`img_`),
the play state icon (`icon_`) and the art glow (`glow_`). `is_always_free()` names them. They keep
their own anchor and offset under every layout, so moving one in the editor does not switch the
widget to free placement, and `detach_to_free_placement()` leaves them alone.

A named style dresses the panel, the art, the bar and each line. Each of them can opt out with
`<prefix>custom` (`bg_custom`, `art_custom`, `bar_custom`, `t0_custom`...), and `resolve_settings()`
then leaves that element's own settings alone. The editor sets that flag for you when you change
something the style decides, copying in what the style was giving the element first, so nothing
jumps.

A design is everything about a widget's look without its music source. `export_design()` and
`apply_design()` turn one into JSON and back; presets, export and import, and copying a design
to another widget all go through them. `builtin_designs()` lists the ones that ship.

Colours that follow the album colour can ask for a tone of it: the colour itself, a dark or light
tone, or black or white chosen to read on it (`widget-palette.cpp`, pure arithmetic). Each tone
is pushed until it reaches a 7:1 contrast with the text it is meant to carry.

The visualiser taps the linked music source with `obs_source_add_audio_capture_callback`. OBS's
audio thread mixes each block down to mono into a ring; the widget's tick copies the ring out and
`Spectrum` (`spectrum.cpp`, pure arithmetic: a Hann window, a radix-2 FFT and log-spaced bands)
turns it into levels. Paused audio stops arriving rather than going quiet, so the visualiser
notices silence by the frame count standing still.

The editor's canvas (`editor-canvas.cpp`) knows nothing about OBS: it shows a picture and
rectangles and reports drags in widget pixels. Snapping, the grid, aligning and distributing are
plain arithmetic in `editor-snap.cpp`. The preview comes from a main render callback that draws
the widget into a texture each frame and copies it back for the canvas.

The editor's inspector is a table of fields per element type (`fields_for()` in
`widget-editor.cpp`), so adding an option to the editor is one line there.

The editor's sample track (`set_widget_sample_track()`) makes the widget show a made-up track,
with a generated cover, in place of its music source. It applies to the widget itself, so OBS's
own preview and output show it too; the editor turns it off when it closes or switches widget.

## Threads, and who owns what

The rule that keeps this simple: **every piece of mutable state has exactly one owning thread.**
Other threads hand it work or read a published copy; they never reach in.

| Thread | Owns | Talks to others by |
|---|---|---|
| Engine (`MusicSource::engine_thread`) | the `Player`, every deck handle, the output timeline | draining `CommandQueue`; publishing `Snapshot` and entries under short locks |
| Deck decode thread (one per `StreamDeck`) | its `AudioDecoder` | a small mutex-guarded buffer the engine reads from |
| Library worker | folder walking, tag reading | posting `set_entries` / `set_tags` commands |
| Art loader (widget) | cover decoding and blurring | a single request/result slot the widget polls |
| OBS audio thread (widget visualiser) | nothing of ours | appending to the widget's audio ring under its mutex |
| OBS audio thread (duck trigger) | nothing of ours | raising an atomic peak the engine takes each block |
| OBS audio thread (Music Reactive) | nothing of ours | appending to the filter's pending audio under its mutex |
| obs-websocket's threads | nothing of ours | the same posting and snapshot reads as the dock |
| Layout editor preview (OBS graphics thread) | its render target and staging surface | a mutex-guarded copy of the last frame the editor's timer picks up |
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
| `src/music/track-tags.*`, `album-art.*` | Tags (ReplayGain and R128 included) and embedded/sidecar cover art via FFmpeg. |
| `src/audio/loudness.*`, `src/music/loudness-scan.*` | The loudness meter, measuring whole files, and the measurement cache. |
| `src/music/ducker.*`, `music-source-duck.cpp` | Ducking: the gain computer, and binding to the trigger source. |
| `src/integrations/websocket.*` | obs-websocket vendor requests and events. |
| `src/filters/music-reactive-filter.*`, `src/audio/beat-detector.*` | The Music Reactive filter and the beat detector it uses. |
| `src/sources/music-widget-source.cpp`, `src/sources/music-widget/` | The Music Widget. `widget.hpp` lists its element types and the few functions its files share; `widget-layout` and `widget-design` are the layouts, styles and designs described above; `widget-visualizer` and `spectrum` the visualiser; `widget-extras` the image, icon and glow; `widget-motion` the pop-in and entrances; `widget-palette` the album colour tones. |
| `src/ui/widget-editor.*`, `editor-canvas.*`, `editor-snap.*` | The layout editor: canvas, layers, inspector, designs. |
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
- **Decoding and seeking**: `AudioDecoder` (formats, seek accuracy, resampling) and
  `StreamDeck` (buffering). `tests/decoder-tests.cpp` writes real files with
  FFmpeg and checks every sample.
- **What a track is called**: `track-tags.cpp` for reading, `PlaylistEntry::display_title` for
  the fallbacks.
- **Anything the widget draws**: `src/sources/music-widget/`; `widget-render.cpp` for drawing,
  `widget-settings.cpp` and `widget-properties.cpp` for its options.
- **A widget layout or style**: a new arrangement goes in `compute_layout()` with a case in
  `tests/widget-layout-tests.cpp` (the sweep there checks it stays tidy at every size); a new
  style is a `StyleTokens` entry in `widget-design.cpp`. Both need an entry in the properties
  list and the editor's combo boxes.
- **A built-in design**: `builtin_designs()` in `widget-design.cpp`; the design tests apply every
  one of them.
- **A new widget element**: its state in `widget.hpp`, defaults in `widget-defaults.cpp`, reading
  and drawing in a file of its own, a `Kind` in `draw_elements()`, an entry in `element_boxes()`
  so the editor sees it, and a row in the editor's `fields_for()`.
- **A template field**: `lookup_field()` and `template_fields()` in `text-template.cpp`, plus a
  `Field.<name>` string; the template test checks every listed field is known.

## Things that are easy to get wrong

- **Never block the engine thread.** It produces audio in real time. Anything that touches the
  disk belongs on a deck thread or the library worker.
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
- **The widget renders through OpenGL on macOS and Linux.** OBS translates effect files to GLSL
  there, and some HLSL that Direct3D accepts does not survive the translation (passing a
  `sampler_state` to a function, for one). Rendering the widget through libobs's OpenGL renderer
  headless is how that was found; an effect change should get the same check.

## Tests

| Test | Covers | Needs |
|---|---|---|
| `player` | transport behaviour end to end, with fake decks, including the queue, volume levelling and skipping silence | nothing |
| `decoder` | decoding, sample-exact seeks, the cover-art seek case, bad files | FFmpeg dev packages |
| `tag` | tags in every container, ReplayGain, the technical details, measuring a file and the cache, cover art choice, sidecar art | FFmpeg dev packages |
| `playlist`, `template` | play order; the now-playing template language | nothing |
| `loudness` | the meter against EBU Tech 3341's reference points, gating, and weighting | nothing |
| `ducker` | ducking depth, attack, hold and release | nothing |
| `beat` | beats and tempo found in grooves loud, quiet and busy; held notes ignored; settling in silence | nothing |
| `widget-layout` | every widget layout, and a sweep of sizes that checks nothing overlaps or spills | nothing |
| `editor-snap` | the layout editor's snapping, grid, aligning and distributing | nothing |
| `editor-canvas` | the editor's canvas driven by simulated mouse and keyboard input: selecting, group moves, the selection band, locking, zoom and the grid | Qt 6 |
| `spectrum` | the visualiser's analysis: tones land in the right band, loudness maps onto levels, smoothing | nothing |
| `palette` | the album colour tones, and that each reads against the text it carries | nothing |
| `widget-design` | layouts and styles resolving into element settings; switching to free placement moves nothing; placing an element lands it where asked; migration; the extras, stacking, designs and every built-in design | libobs |
| `obs-smoke` | the built plugin inside a real headless libobs | libobs, an X display |

`README.md` has the commands. CI runs everything except `obs-smoke` on every push and pull
request.

## Style

- `clang-format` 19 for C++ under `src/` and `gersemi` for CMake, both checked in CI. The
  settings are in `.clang-format` and `.gersemirc`.
- Comments say why, not what. When a line exists because of an OBS or FFmpeg quirk, the comment
  names the quirk, so the next person does not "simplify" it away.
