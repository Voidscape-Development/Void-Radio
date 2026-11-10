# Void Radio

Music playback for OBS Studio: a playlist source that focuses purely on audio, a now-playing
widget, a progress bar source that tracks it, and a filter that feeds now-playing text into any
text source.

## What's in it

### Music Source

A playlist driven audio source, in the spirit of the VLC playlist source but audio only and
built around music playback.

* Add individual files and whole folders, optionally including subfolders.
* Shuffle, repeat all, repeat one, or stop at the end of the list.
* Crossfade between tracks, or a plain silent gap when crossfade is off. Crossfading applies to
  natural track ends and to manual skips alike, and is clamped so a short track cannot be
  swallowed by the fade. With both off, tracks run straight into each other with no gap at all.
* Seeking lands on the exact position asked for, from the dock, OBS's media controls or
  anything else that drives the source.
* Independent fade in and fade out for starting, pausing and stopping.
* Song titles and artists are read from the file's own tags. Anything missing can be filled in
  by hand per track from the dock, and those edits are saved with the scene collection.
* `Keep playing when hidden` (on by default) keeps the music going when you cut to a scene that
  does not contain the source. Turn it off to have playback pause whenever the source is not on
  the program output.
* Global hotkeys for play/pause, stop, next, previous and restart.
* Appears in OBS's own media controls, so the built-in transport buttons work too.
* **Even out the volume** so one track is not much louder than the next. Each track is turned up
  or down towards a target loudness (in LUFS) taken from its ReplayGain or R128 tags, or, for
  files without them, measured once in the background and remembered. Album mode keeps a tagged
  album's quiet and loud tracks as they were mastered, and a guard stops a quiet track with loud
  peaks from being boosted into clipping.
* **Turn the music down while someone talks.** Pick a source to listen to, usually your
  microphone; while it is louder than the threshold the music goes down by as much as you ask,
  stays down through short pauses, and comes back up afterwards. Attack, hold and release times
  are all adjustable.
* **Skip silence** at the start and end of tracks, so there is no dead air between songs. Quiet
  audio before a track starts is skipped at once, and a track whose last half minute has gone
  quiet for a second and a half ends there. A quiet passage in the middle of a track is never cut,
  and seeking always lands where it was asked to.

Audio is decoded with FFmpeg (the copy OBS already ships) and mixed inside the Music Source,
so the music arrives in the audio mixer as a single channel with working volume, filters,
monitoring and track routing.

### Music Widget

A self-contained now-playing card: cover art, text and a progress bar in one source, with no
browser source and no second application involved.

Setting one up comes down to three choices:

* **Layout.** Art on the left or right, stacked with the art on top, the art filling the card
  behind the text, a compact bar, or text only. Positions and sizes are worked out from the
  widget's size, so resizing the widget reflows the card instead of leaving elements where they
  were.
* **Style.** Glass (the cover art blurred behind everything), Solid, Minimal, Outline, Vinyl (a
  spinning record), Neon (a glowing title and an accent border), Cassette (warm and retro) or
  Ticker (a square strip in a dark tone of the album colour). A style sets the panel, corners,
  art shape, type and colours for the whole card, with one font, one text size, and an accent
  colour taken from the album art or fixed.
* **Lines.** Up to four lines of template text on the main view, each with a role (title,
  subtitle, detail or time) that decides its size and colour. Two more lines are available in
  the advanced options.

Everything else waits behind **Show advanced options**: spacing, art size and shape, corner
radius, animations, and for each line and element the full set of options below. Any line, and
the panel, art and progress bar, can opt out of the style and keep its own look, and the
**Custom** style leaves every element's look to you.

**Free placement** lets you put each element exactly where you want it. Each one picks an anchor
(one of the nine corners, edges or the centre) and is offset from there in pixels, so a
right-anchored element follows the right edge when the widget is resized.

The **layout editor** (from the widget's properties, or **Tools → Music Widget Layout Editor**)
shows the widget as OBS draws it.

* **Moving things.** Drag any element to move it, or a corner or edge to resize it. Shift or
  Ctrl click to select several, or drag across empty space to select everything the band
  touches, then move them together. Elements snap to the widget's edges and centre lines and to
  each other (hold Alt to stop snapping), or to a grid if you turn one on. Arrow keys nudge, by
  ten with Shift. Buttons line the selection up along an edge or centre, or space three or more
  out evenly; with one element selected they line it up with the widget.
* **Zoom.** Ctrl and the scroll wheel zoom around the mouse, the wheel scrolls, the middle button
  drags the view, and **Fit** puts it back.
* **Layers.** The list beside the canvas runs front to back. Tick an element to show it, tick its
  padlock to stop it being dragged by accident, and move it forwards or backwards with the
  buttons underneath. The panel is listed too, so it can be styled and restacked.
* **Inspector.** Below the layers: the selected element's position and size, then its own
  options: a line's text and its text when idle, with a menu of every field and a preview of
  what the line says right now; its font, colour, album colour tone, alignment and long text
  handling; and the options of the art, bar, panel and extras. Changing something the style
  decides makes that element keep its own look from then on, starting from what the style gave
  it, so nothing jumps.
* **Sample track.** Shows a made-up track with a cover, so the widget can be designed with
  nothing playing; **Long titles** tries out how long text is handled, and **Replay track
  change** plays the transition, pop-in and entrances again. OBS shows the sample track on that
  widget too, until it is switched off or the editor is closed.
* **Designs.** Built-in designs (a lower third, now and up next, a big cover with a visualiser,
  a ticker, Neon, Cassette, and one that pops in on each track), designs of your own saved by
  name, export to and import from a file, and copying this widget's design onto another. A
  design never carries the music source, so it can be shared.
* **Undo and Redo**, with typing into a box or dragging a slider counted as one step.

Dragging an element under an automatic layout switches the widget to free placement, starting
from where everything already is, so nothing jumps. Under a style, a line's type follows the
height of its box, so making a line taller makes its text bigger.

Widgets made before layouts and styles existed open on free placement and the Custom style, so
they look exactly as they did.

The elements the widget draws:

* **Background panel.** A solid colour, a linear gradient, an image file, or the current cover
  art blurred, darkened and desaturated by however much you ask for. Corner radius, border, and
  an inset from the widget's edge.
* **Album art.** Read from the track's own tags: ID3v2 pictures in MP3, the FLAC picture block,
  `METADATA_BLOCK_PICTURE` in Ogg Vorbis and Opus, and the `covr` atom in MP4/M4A. When a
  file carries none, a `cover`, `folder`, `front`, `album`, `albumart`, `artwork` or `thumb`
  image sitting in the same folder is used instead, and failing that a placeholder you choose.
  Fill the box and crop, fit inside it, or stretch; corner radius, border, drop shadow, a fixed
  rotation, and optionally spin like a record or pulse while the music plays.
* **Six lines of text**, each holding a template rather than a fixed field, so a line can be
  `{title}`, or `{artist} — {album}`, or `{elapsed} / {duration}`. The same syntax the Music Info
  filter uses, including the `[optional]` brackets. Each line has its own font, colour or
  gradient fill, outline, drop shadow, letter spacing, line height, alignment, letter case and
  rotation. A line whose template comes out empty draws nothing, so an untagged track leaves no
  gap, and each line has a separate template for when nothing is playing. That idle template is
  the whole of what the line says while idle, so leaving it empty leaves the line blank rather
  than carrying on with the last track's details.
* **Progress bar**, the same one the standalone source draws, with all of its fill types,
  directions and animations.
* **Audio visualiser**: bars, mirrored bars, a waveform, or a ring to put around the art, moving
  with the music source's own sound. Bar count, gap, rounded ends, a second colour at the tips,
  sensitivity, smoothing and frequency range.
* **Album art glow**: the cover blurred inside a box of its own, to sit behind the art.
* **Image**: a picture of your own, such as a channel logo.
* **Play state icon**: play, pause or stop, all the time or only when the music is not playing.

The visualiser, glow, image and icon are placed by hand under any layout, so adding one does not
switch the widget to free placement.

Long titles are handled per line: scroll them (looping with a gap, or running to the end and
back with a pause), shrink the type until it fits, truncate with an ellipsis from either end or
the middle, or wrap onto more lines.

The progress bar fill, any border, the panel and any line of text can be set to **follow the
album colour**, which is sampled from the current cover, so the whole card re-themes itself with
each track. Anything that follows it can take a **tone** of it: the colour itself, a dark tone
that white text reads on, a light tone that dark text reads on, or black or white, whichever
stands out against it. A line's shadow can follow it too, which makes a glow.

Templates can use `{album_artist}`, `{composer}`, `{comment}`, `{bpm}`, `{disc}`, `{codec}`,
`{bitrate}`, `{samplerate}`, `{position}` ("3 / 12") and `{next_album}` as well as the fields the
Music Info filter has always had.

On a track change the card can cross-fade, slide across, fade out and back in, or swap
instantly. It can also be set to **show itself only when a track starts**: it comes in (fading,
sliding from any side, or growing), stays up for as long as you choose, then goes again, and a
hotkey brings it back. Its elements can **arrive one after another** as it does, fading,
sliding or growing into place in reading order. When nothing is playing it can hide itself, show the placeholder image with the idle
templates, or keep showing the last track — and pausing can be made to count as nothing playing
too, which it does not by default, a paused track still being the current track. Keeping the
last track on screen goes on using the playing templates for any line whose idle template is
empty, since holding onto the last track is the point of it.


### Music Progress Bar

A video source that shows how far along the selected Music Source is.

* Width, height, corner radius, and an optional border.
* Background and progress layers are independently a solid colour, a linear gradient, an image
  file, or the rendered output of another source in the scene.
* Fills left to right, right to left, bottom to top, top to bottom, or from the centre out.
* Movement is interpolated between decoder updates, so the fill glides rather than stepping once
  per audio packet.
* On a track change the bar animates: it fills to the end and sweeps back to the start
  (configurable duration), eases back from wherever it was, or jumps instantly. Playback ending
  runs the same animation, down to an empty bar.
* Choose what it does when nothing is playing, or when a track's length is unknown: sit empty,
  hide, or run a looping sweep. Pausing can be made to count as nothing playing as well, which
  it does not by default.

### Music Info

A filter you add to a text source. It reads from a Music Source and keeps the text updated.

Write a template using any of these fields:

| | | | |
|---|---|---|---|
| `{title}` | `{artist}` | `{album}` | `{album_artist}` |
| `{year}` | `{genre}` | `{track}` | `{disc}` |
| `{composer}` | `{comment}` | `{bpm}` | `{filename}` |
| `{path}` | `{elapsed}` | `{duration}` | `{remaining}` |
| `{percent}` | `{state}` | `{index}` | `{count}` |
| `{position}` | `{next_title}` | `{next_artist}` | `{next_album}` |
| `{codec}` | `{bitrate}` | `{samplerate}` | |

`{position}` reads "3 / 12". `{codec}`, `{bitrate}` and `{samplerate}` describe the file itself
("FLAC", "1024 kbps", "44.1 kHz"), so they are there even for an untagged track.

Text wrapped in square brackets disappears when a field inside it is empty, so
`{title}[ - {artist}]` loses the dash on an untagged track. Use `\[`, `\]`, `\{`, `\}` for
literal brackets and braces. A separate template can be set for when playback is stopped.

Elapsed and total times can be formatted as `m:ss`, `mm:ss`, `h:mm:ss`, `hh:mm:ss`, or
automatically (adding the hours field only once a track passes an hour).

### Music Reactive

A filter you add to any source - a webcam, a logo, an image, a whole scene - that makes it move
with the music playing in a Music Source.

* **Moves with** the beat (a sharp kick on each hit), the bass, the overall loudness, or the
  treble. The continuous modes stretch between how quiet and how loud the music has recently
  been, so a loudly mastered track still swings.
* **Pulse the size** from the middle. Kept inside its box (the default), it rests a little smaller
  and grows to full size on the beat, so nothing is ever cut off.
* **Brighten**, **flash a colour**, **fade between beats**, and **shake**, in any combination,
  each with its own amount.
* Sensitivity, and how long each kick takes to settle back.

Beats are found as sudden rises in the bass against the last few hundredths of a second, so a
held bass note never fires however loud it is and a kick over a busy bass line still does. When
the music is paused or stopped, the source settles back to rest.

### Now Playing dock

Under **View → Docks → Now Playing**: pick a Music Source, see the current title and artist,
scrub with the seek bar, and use the transport, shuffle and repeat controls. The playlist below
can be reordered by dragging, played by double-clicking, and each track has a context menu for
playing it next, adding it to the queue, editing its title and artist, or removing it from the
list.

**The queue.** Queued tracks play before the playlist carries on, in the order they were queued,
with "play next" jumping the line; once the queue runs out, the playlist carries on from where it
was. An **Up next** list above the playlist shows the queue: drag to reorder it, right-click to
move a track to the top or take it out, or clear it. Queued tracks are marked in the playlist,
and `{next_title}`, `{next_artist}` and the widget's up-next lines follow the queue. Repeat-one
keeps repeating until you skip; previous from a queued track goes back to where the queue began.
The queue lasts for the session and is not saved with the scene collection.

### Remote control (obs-websocket)

When obs-websocket is installed (it ships with OBS 28 and later), Void Radio registers vendor
requests under the vendor name `void-radio`, so Stream Deck, Streamer.bot, Touch Portal, chat bots
and anything else that speaks obs-websocket can drive the music. Send them with
`CallVendorRequest`:

| Request | Request data | Does |
|---|---|---|
| `GetMusicSources` | | Lists every music source with what it is playing |
| `GetNowPlaying` | | State, title, artist, album, position, length, playlist position, next track |
| `GetPlaylist` | | Every entry, the current index, the queue, shuffle and repeat |
| `Play`, `Pause`, `TogglePause`, `Stop`, `Next`, `Previous`, `Restart` | | Transport |
| `Seek` | `positionMs` | Moves within the track |
| `PlayIndex` | `index` | Plays an entry, counting from 0 |
| `SetShuffle` | `shuffle` (true/false) | |
| `SetRepeat` | `mode` (`off`, `all`, `one`) | |
| `PlayNext`, `AddToQueue` | `index` | Queues an entry at the front or the back |
| `RemoveFromQueue` | `position` | Takes the entry at that place in the queue out, counting from 0 |
| `ClearQueue` | | Empties the queue |

Every request also takes an optional `sourceName` or `sourceUuid`; without one it goes to the
first music source. Every response has `ok`, and `error` when it is false. For example, with
obs-websocket's own request format:

```json
{"requestType": "CallVendorRequest", "requestData": {
  "vendorName": "void-radio", "requestType": "Next", "requestData": {"sourceName": "Music"}}}
```

Two vendor events go out to subscribers of `VendorEvent`: `NowPlayingChanged`, when a track
starts or its details arrive, and `PlaybackStateChanged`, on play, pause and stop. Both carry the
same fields as `GetNowPlaying`.

## Installing

Each release ships two sets of files. Pick the one that matches your OBS Studio version: a build
only loads in the OBS version line it was built for, because OBS Studio 33 removed image APIs the
older builds link against.

| OBS Studio | Windows | macOS | Ubuntu |
|---|---|---|---|
| 32 and earlier | `void-radio-<ver>-windows-x64.zip` / `-Portable.zip` | `void-radio-<ver>-macos-universal.pkg` | `void-radio-<ver>-x86_64-linux-gnu.deb` (24.04) |
| 33+ | `void-radio-<ver>-windows-x64-obs33.zip` / `-obs33-Portable.zip` | `void-radio-<ver>-macos-universal-obs33.pkg` | `void-radio-<ver>-x86_64-linux-gnu-obs33.deb` (26.04) |

On Windows, the OBS 33+ files use the new third-party plugin layout, so OBS does not flag the
plugin as Legacy:

* **Installed OBS:** extract `void-radio-<ver>-windows-x64-obs33.zip` into
  `C:\ProgramData\obs-studio\plugins\`, giving `...\plugins\void-radio\void-radio.dll` and
  `...\plugins\void-radio\data\`.
* **Portable OBS:** extract `void-radio-<ver>-windows-x64-obs33-Portable.zip` into the OBS folder,
  giving `<OBS folder>\plugins\void-radio\`.

If you are upgrading from an older install, remove the old copy from `C:\Program Files\obs-studio\obs-plugins\64bit\`
or `C:\ProgramData\obs-studio\plugins\void-radio\bin\64bit\`.

## Building

Standard OBS plugin template build; see the
[plugin template wiki](https://github.com/obsproject/obs-plugintemplate/wiki) for the full
details.

| Platform | Toolchain |
|---|---|
| Windows | Visual Studio 17 2022, CMake 3.30.5 |
| macOS | Xcode 16.0, CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3, `ninja-build`, `pkg-config`, `build-essential`, FFmpeg development packages |

```sh
cmake --preset ubuntu-x86_64   # or windows-x64 / macos
cmake --build --preset ubuntu-x86_64
```

The plugin links FFmpeg (`avformat`, `avcodec`, `avutil`, `swresample`) to decode audio and read
tags. On Windows and macOS it comes with the OBS dependencies the build already downloads; on
Ubuntu install `libavcodec-dev libavformat-dev libavutil-dev libswresample-dev`. Either way it is
the same FFmpeg OBS itself runs on, so nothing extra ships with the plugin.

How the code is laid out, which thread owns what, and where to make a given change is described
in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

### Building for OBS Studio 33+

Each preset has an `-obs33` twin (`windows-x64-obs33`, `macos-obs33`, `ubuntu-x86_64-obs33`, and
the `-ci-` equivalents). They build the same sources against the OBS 33 SDK pinned under
`sdkVariants.obs33` in `buildspec.json`, in their own build and `.deps_obs33` directories. The
CI scripts pick these up when `OBS_SDK_VARIANT=obs33` is set.

| Platform | Toolchain (OBS 33+) |
|---|---|
| Windows | Visual Studio 18 2026, Windows SDK 10.0.26100 |
| macOS | Xcode 26.5 |
| Ubuntu 26.04 | `obs-studio` 33+ from the OBS PPA |

On Windows the OBS 33+ build installs as `void-radio/void-radio.dll` + `void-radio/data/`
(the new layout) instead of `void-radio/bin/64bit/`.

The plugin needs Qt 6 for the Music Widget, which draws its own glyphs and decodes its own cover
art, and both Qt 6 and `obs-frontend-api` for the dock. Both are on by default and provided by
the template's dependency setup; without them the plugin still builds, it just has no widget
source and no dock.

## Tests

Everything that does not need OBS builds and runs on its own: the playback engine's transport
logic (against fake decks), the decoder and its seeking (against real files the tests write with
FFmpeg), tag and cover art reading, the template expander and playlist navigation. They need the
FFmpeg development packages and run in CI on every push and pull request.

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

On Linux there is also a smoke test that loads the built plugin into a real, headless libobs and
plays a playlist through it. It is built when libobs is installed and runs when pointed at the
plugin; it needs an X display, so run it under `xvfb-run`:

```sh
cmake -S tests -B build_tests -DVOID_RADIO_PLUGIN=$PWD/build_x86_64/void-radio.so
cmake --build build_tests
xvfb-run -a ctest --test-dir build_tests --output-on-failure
```

## Notes

* Tags and cover art are read with FFmpeg: ID3v2 and ID3v1 in MP3, Vorbis comments in FLAC and
  Ogg (Vorbis, Opus), iTunes-style metadata in MP4/M4A, RIFF INFO in WAV, and whatever else
  FFmpeg can open. Anything unreadable falls back to the file name, and can be overridden by
  hand.
* Cover art is decoded off the graphics thread so a large JPEG cannot stutter the render. WAV has
  no standard picture chunk, so those tracks rely on a sidecar image or the placeholder.
* Anything FFmpeg can decode plays, which is far wider than that list.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
