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

#pragma once

#include "music/snapshot.hpp"

#include <obs-module.h>
#include <graphics/image-file.h>

#include <cstdint>
#include <string>

/*
 * The progress bar, factored out so that the standalone Music Progress Bar
 * source and the bar element inside the Music Widget draw the same thing with
 * the same options.
 *
 * Every setting key is written with a caller supplied prefix. The standalone
 * source passes an empty prefix, which reproduces exactly the keys it has
 * always used, so existing scene collections keep working.
 */

namespace vr {

enum class FillType {
	Color = 0,
	Gradient = 1,
	Image = 2,
	Source = 3,
};

enum class BarDirection {
	LeftToRight = 0,
	RightToLeft = 1,
	BottomToTop = 2,
	TopToBottom = 3,
	CenterOut = 4,
};

enum class ResetStyle {
	FillThenReset = 0,
	EaseBack = 1,
	Instant = 2,
};

enum class IdleMode {
	Empty = 0,
	Hide = 1,
	Sweep = 2,
};

/* What counts as "nothing playing". A paused track is still the current track,
 * so holding the bar where it was is the default. */
enum class IdleTrigger {
	Stopped = 0,
	StoppedOrPaused = 1,
};

/* One drawable layer of the bar: the track behind the fill, or the fill. */
struct FillLayer {
	FillType type = FillType::Color;
	uint32_t color = 0xFF1E1E1E;
	uint32_t color2 = 0xFF000000;
	float gradient_angle = 0.0f;
	float opacity = 1.0f;
	bool tile = false;

	/* When set, `color` is replaced by the accent colour taken from the
	 * current cover art. Only offered where an accent is available. */
	bool follow_accent = false;

	std::string image_path;
	gs_image_file4_t image = {};
	bool image_valid = false;

	std::string source_name;
	obs_weak_source_t *weak_source = nullptr;
	gs_texrender_t *texrender = nullptr;

	/* Filled in during render. */
	gs_texture_t *texture = nullptr;
	uint32_t texture_cx = 0;
	uint32_t texture_cy = 0;
};

class BarRenderer {
public:
	BarRenderer() = default;
	~BarRenderer();

	BarRenderer(const BarRenderer &) = delete;
	BarRenderer &operator=(const BarRenderer &) = delete;

	/* Loads the shared effect. Safe to call more than once. */
	void create();

	/* Releases textures, render targets and weak references. Called from
	 * the owning source's destroy callback. */
	void destroy();

	void update(obs_source_t *self, obs_data_t *settings, const std::string &prefix);

	/* Advances the fill animation. `snapshot` may be null when no music
	 * source is selected, which puts the bar into its idle behaviour. */
	void tick(float seconds, const Snapshot *snapshot);

	/*
	 * Resolves the layer textures, which for a source backed layer means
	 * rendering that source into a texture of its own. Must be called
	 * before render, and must not be called inside another render target,
	 * so the Music Widget calls it before it starts compositing.
	 */
	void prepare(uint32_t width, uint32_t height);

	/* Draws at the current matrix, `width` by `height` pixels, with the
	 * top left corner at the origin. Call prepare first. */
	void render(uint32_t width, uint32_t height);

	/* True when the bar has nothing to show and is set to hide. */
	bool hidden() const { return idle_ && idle_mode_ == IdleMode::Hide; }

	/* Only meaningful when the bar owns its own size, which is to say for
	 * the standalone source. */
	uint32_t width() const { return width_; }
	uint32_t height() const { return height_; }

	/* Cover art accent, used by layers with `follow_accent` set. */
	void set_accent(uint32_t color, bool valid);

	void enum_active_sources(obs_source_t *self, obs_source_enum_proc_t callback, void *param) const;

	/*
	 * Properties and defaults. `include_size` adds the width and height
	 * fields, which only the standalone source owns; the widget sizes its
	 * bar from the element's own geometry instead. `include_accent` adds
	 * the "follow the album colour" option to each layer.
	 */
	static void add_properties(obs_properties_t *props, const std::string &prefix, bool include_size,
				   bool include_accent);
	static void add_defaults(obs_data_t *settings, const std::string &prefix);

private:
	/* Starts the chosen "on track change" animation towards `to`, or lands
	 * on it at once when there is no animation to run. */
	void begin_reset(float to);

	void update_layer(obs_source_t *self, FillLayer &layer, obs_data_t *settings, const std::string &prefix);
	void resolve_layer_texture(FillLayer &layer, uint32_t width, uint32_t height);
	void set_layer_params(const FillLayer &layer, const char *prefix, uint32_t width, uint32_t height,
			      bool linear_srgb) const;

	gs_effect_t *effect_ = nullptr;

	uint32_t width_ = 480;
	uint32_t height_ = 24;

	FillLayer background_;
	FillLayer foreground_;

	BarDirection direction_ = BarDirection::LeftToRight;
	float corner_radius_ = 6.0f;
	float border_width_ = 0.0f;
	uint32_t border_color_ = 0xFF000000;
	bool border_follow_accent_ = false;

	ResetStyle reset_style_ = ResetStyle::FillThenReset;
	float reset_seconds_ = 0.3f;
	IdleMode idle_mode_ = IdleMode::Empty;
	IdleTrigger idle_trigger_ = IdleTrigger::Stopped;

	uint32_t accent_ = 0;
	bool accent_valid_ = false;

	/* Animation state. */
	uint64_t last_serial_ = 0;
	bool have_serial_ = false;
	float displayed_ = 0.0f;
	float target_ = 0.0f;
	bool animating_ = false;
	float animation_time_ = 0.0f;
	float animation_from_ = 0.0f;
	float sweep_position_ = 0.0f;
	bool idle_ = true;

	/* How long a track has been playing without reporting a length. A track
	 * change goes through such a moment, and treating it as idle straight
	 * away is what used to throw the animation away. */
	float length_wait_ = 0.0f;
};

} // namespace vr
