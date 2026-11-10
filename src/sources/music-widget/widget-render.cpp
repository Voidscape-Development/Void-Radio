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

/* Drawing the card: each element, then the track change transition. */

#include "sources/music-widget/widget.hpp"

#include <graphics/matrix4.h>

#include <algorithm>
#include <cmath>

namespace vr::widget {

/* ------------------------------------------------------------------------- */
/* Render                                                                     */
/* ------------------------------------------------------------------------- */

static void draw_background(MusicWidget *widget)
{
	const BackgroundElement &background = widget->background;

	if (!background.enabled)
		return;

	const float inset = std::max(background.inset, 0.0f);
	const float width = (float)widget->width - inset * 2.0f;
	const float height = (float)widget->height - inset * 2.0f;

	if (width <= 0.0f || height <= 0.0f)
		return;

	DrawParams params;
	params.x = inset;
	params.y = inset;
	params.width = width;
	params.height = height;
	params.corner_radius = background.corner_radius;
	params.border_width = background.border_width;
	params.border_color = resolve_color(widget, background.border_color, background.border_accent);
	params.opacity = background.opacity;
	params.color = resolve_color(widget, background.color, background.follow_accent, background.tone);
	params.color2 = resolve_color(widget, background.color2, background.follow_accent2, background.tone2);
	params.gradient_angle = background.gradient_angle;

	switch (background.type) {
	case BackgroundType::Gradient:
		params.fill_mode = 1;
		break;

	case BackgroundType::Image:
		if (background.image_valid) {
			params.fill_mode = 2;
			params.texture = vr_image_file_base(background.image).texture;
			params.color = 0xFFFFFFFF;
		}
		break;

	case BackgroundType::BlurredArt:
		if (widget->blur_texture) {
			params.fill_mode = 2;
			params.texture = widget->blur_texture;
			params.color = 0xFFFFFFFF;
		}
		break;

	default:
		break;
	}

	draw_quad(widget->effect, params, false);
}

static void draw_art(MusicWidget *widget)
{
	const ArtElement &art = widget->art;

	if (!art.geometry.enabled || !widget->art_texture)
		return;

	float x;
	float y;
	resolve_position(art.geometry, widget->width, widget->height, x, y);

	float width = art.geometry.width;
	float height = art.geometry.height;

	DrawParams params;
	params.fill_mode = 2;
	params.texture = widget->art_texture;
	params.corner_radius = art.corner_radius;
	params.border_width = art.border_width;
	params.border_color = resolve_color(widget, art.border_color, art.border_accent);
	params.opacity = art.geometry.opacity * widget->draw_opacity;
	params.color = 0xFFFFFFFF;

	const float texture_width = (float)gs_texture_get_width(widget->art_texture);
	const float texture_height = (float)gs_texture_get_height(widget->art_texture);

	if (texture_width > 0.0f && texture_height > 0.0f) {
		const float source_aspect = texture_width / texture_height;
		const float box_aspect = width / height;

		if (art.fit == ArtFit::Cover) {
			/* Crop in UV space so the box stays exactly as placed. */
			if (source_aspect > box_aspect) {
				params.uv_scale_x = box_aspect / source_aspect;
				params.uv_offset_x = (1.0f - params.uv_scale_x) * 0.5f;
			} else if (source_aspect < box_aspect) {
				params.uv_scale_y = source_aspect / box_aspect;
				params.uv_offset_y = (1.0f - params.uv_scale_y) * 0.5f;
			}
		} else if (art.fit == ArtFit::Contain) {
			/* Shrink the quad instead, so the letterboxed area is
			 * genuinely empty rather than smeared edge pixels. */
			if (source_aspect > box_aspect) {
				const float fitted = width / source_aspect;
				y += (height - fitted) * 0.5f;
				height = fitted;
			} else if (source_aspect < box_aspect) {
				const float fitted = height * source_aspect;
				x += (width - fitted) * 0.5f;
				width = fitted;
			}
		}
	}

	params.x = x;
	params.y = y;
	params.width = width;
	params.height = height;

	if (art.animation == ArtAnimation::Spin)
		params.rotation = art.rotation + art.spin_angle;
	else
		params.rotation = art.rotation;

	if (art.animation == ArtAnimation::Pulse) {
		const float phase = art.pulse_time * art.pulse_rate * 2.0f * PI;
		params.scale = 1.0f + std::sin(phase) * art.pulse_percent / 100.0f;
	}

	if (art.shadow) {
		DrawParams shadow;
		shadow.x = x + art.shadow_x;
		shadow.y = y + art.shadow_y;
		shadow.width = width;
		shadow.height = height;
		shadow.corner_radius = art.corner_radius;
		shadow.softness = art.shadow_blur;
		shadow.color = art.shadow_color;
		shadow.opacity = params.opacity;
		shadow.rotation = params.rotation;
		shadow.scale = params.scale;

		draw_quad(widget->effect, shadow, false);
	}

	draw_quad(widget->effect, params, false);
}

static void draw_text(MusicWidget *widget, TextElement &element)
{
	if (!element.geometry.enabled || !element.texture)
		return;

	if (element.hide_empty && element.current.empty())
		return;

	float x;
	float y;
	resolve_position(element.geometry, widget->width, widget->height, x, y);

	/* The pixel buffer is released once uploaded, so the texture itself is
	 * the only remaining record of how big the raster came out. */
	const float full_width = (float)gs_texture_get_width(element.texture);
	const float texture_height = (float)gs_texture_get_height(element.texture);

	DrawParams params;
	params.fill_mode = 2;
	params.texture = element.texture;
	params.opacity = element.geometry.opacity * widget->draw_opacity;
	params.rotation = element.rotation;
	params.color = 0xFFFFFFFF;

	if (element.raster.scrolls && full_width > 0.0f) {
		const float offset = marquee_offset(element.raster, element.raster_style,
						    (uint32_t)element.geometry.width, element.scroll_time);

		params.x = x;
		params.y = y - (float)element.raster.pad;
		params.width = element.geometry.width;
		params.height = texture_height;
		params.uv_scale_x = element.geometry.width / full_width;
		params.uv_offset_x = offset / full_width;
	} else {
		params.x = x - (float)element.raster.pad;
		params.y = y - (float)element.raster.pad;
		params.width = full_width;
		params.height = texture_height;
	}

	draw_quad(widget->effect, params, true);
}

static void draw_bar(MusicWidget *widget)
{
	if (!widget->bar_geometry.enabled || widget->bar.hidden())
		return;

	float x;
	float y;
	resolve_position(widget->bar_geometry, widget->width, widget->height, x, y);

	const uint32_t width = (uint32_t)std::max(1.0f, widget->bar_geometry.width);
	const uint32_t height = (uint32_t)std::max(1.0f, widget->bar_geometry.height);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);

	widget->bar.set_opacity(widget->draw_opacity);
	widget->bar.render(width, height);
	widget->bar.set_opacity(1.0f);

	gs_matrix_pop();
}

/* What each drawable is, for sorting and for the entrance animation. */
enum class Kind {
	Background,
	Glow,
	Art,
	Image,
	Icon,
	Text,
	Bar,
	Visualizer,
};

/* Everything the widget draws, in the order the z values ask for. */
static void draw_elements(MusicWidget *widget)
{
	struct Item {
		int z;
		int order;
		Kind kind;
		int slot;
		const Geometry *geometry;
	};

	std::vector<Item> items;
	items.reserve(TEXT_SLOTS + 8);

	int order = 0;
	const auto add = [&](bool enabled, int z, Kind kind, int slot, const Geometry *geometry) {
		if (enabled)
			items.push_back({z, order, kind, slot, geometry});
		order++;
	};

	add(widget->background.enabled, widget->background.z, Kind::Background, 0, nullptr);
	add(widget->glow.geometry.enabled, widget->glow.geometry.z, Kind::Glow, 0, &widget->glow.geometry);
	add(widget->art.geometry.enabled, widget->art.geometry.z, Kind::Art, 0, &widget->art.geometry);
	add(widget->image.geometry.enabled, widget->image.geometry.z, Kind::Image, 0, &widget->image.geometry);
	add(widget->bar_geometry.enabled, widget->bar_geometry.z, Kind::Bar, 0, &widget->bar_geometry);
	add(widget->viz.geometry.enabled, widget->viz.geometry.z, Kind::Visualizer, 0, &widget->viz.geometry);

	for (int slot = 0; slot < TEXT_SLOTS; slot++)
		add(widget->text[slot].geometry.enabled, widget->text[slot].geometry.z, Kind::Text, slot,
		    &widget->text[slot].geometry);

	add(widget->icon.geometry.enabled, widget->icon.geometry.z, Kind::Icon, 0, &widget->icon.geometry);

	/* Equal z values keep their declaration order, so the panel is behind
	 * the art is behind the text unless told otherwise. */
	std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
		if (a.z != b.z)
			return a.z < b.z;
		return a.order < b.order;
	});

	/* Entrances run top to bottom, left to right, which is the order people
	 * read the card in, rather than the order it is stacked in. */
	std::vector<const Item *> reading;
	for (const Item &item : items)
		if (item.geometry)
			reading.push_back(&item);

	const auto top_left = [widget](const Item *item, float &x, float &y) {
		resolve_position(*item->geometry, widget->width, widget->height, x, y);
	};
	std::stable_sort(reading.begin(), reading.end(), [&](const Item *a, const Item *b) {
		float ax, ay, bx, by;
		top_left(a, ax, ay);
		top_left(b, bx, by);
		if (std::fabs(ay - by) > 4.0f)
			return ay < by;
		return ax < bx;
	});

	const PlayState state = widget->have_snapshot ? widget->snapshot.state : PlayState::Stopped;

	for (const Item &item : items) {
		int place = -1;
		for (size_t i = 0; i < reading.size(); i++)
			if (reading[i] == &item)
				place = (int)i;

		const Reveal reveal = element_reveal(widget, place);
		const bool moved = reveal.dx != 0.0f || reveal.dy != 0.0f || reveal.scale != 1.0f;

		widget->draw_opacity = reveal.opacity;

		if (moved) {
			float x = 0.0f, y = 0.0f;
			resolve_position(*item.geometry, widget->width, widget->height, x, y);
			const float cx = x + item.geometry->width * 0.5f;
			const float cy = y + item.geometry->height * 0.5f;

			gs_matrix_push();
			gs_matrix_translate3f(cx + reveal.dx, cy + reveal.dy, 0.0f);
			gs_matrix_scale3f(reveal.scale, reveal.scale, 1.0f);
			gs_matrix_translate3f(-cx, -cy, 0.0f);
		}

		switch (item.kind) {
		case Kind::Background:
			draw_background(widget);
			break;
		case Kind::Glow:
			draw_glow(widget);
			break;
		case Kind::Art:
			draw_art(widget);
			break;
		case Kind::Image:
			draw_image(widget);
			break;
		case Kind::Icon:
			draw_icon(widget, state);
			break;
		case Kind::Bar:
			draw_bar(widget);
			break;
		case Kind::Visualizer:
			draw_visualizer(widget);
			break;
		case Kind::Text:
			draw_text(widget, widget->text[item.slot]);
			break;
		}

		if (moved)
			gs_matrix_pop();
	}

	widget->draw_opacity = 1.0f;
}

static void ensure_previous_texture(MusicWidget *widget)
{
	if (widget->previous && widget->previous_width == widget->width && widget->previous_height == widget->height)
		return;

	if (widget->previous) {
		gs_texture_destroy(widget->previous);
		widget->previous = nullptr;
	}

	widget->previous = gs_texture_create(widget->width, widget->height, GS_RGBA, 1, nullptr, GS_RENDER_TARGET);
	widget->previous_width = widget->width;
	widget->previous_height = widget->height;
	widget->previous_valid = false;
}

void widget_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);

	MusicWidget *widget = static_cast<MusicWidget *>(data);

	if (!widget->effect || !widget->composite)
		return;

	if (widget->idle && widget->idle_mode == WidgetIdle::Hide)
		return;

	const Reveal shown = popup_reveal(widget);
	if (shown.opacity <= 0.0f)
		return;

	/* Source backed bar layers render into targets of their own, which has
	 * to happen before this source opens one. */
	widget->bar.prepare((uint32_t)std::max(1.0f, widget->bar_geometry.width),
			    (uint32_t)std::max(1.0f, widget->bar_geometry.height));

	gs_texrender_reset(widget->composite);

	/*
	 * draw_quad hands the shader colours and textures already converted to
	 * linear whenever OBS is working that way, so the framebuffer has to be
	 * told to encode back to sRGB on the way out. Without it every colour
	 * lands darker and more saturated than it was picked - a pink panel
	 * arriving on screen as red - and the card looks like it has a shadow
	 * cast over it.
	 */
	const bool linear_srgb = gs_get_linear_srgb();
	const bool previous_srgb = gs_framebuffer_srgb_enabled();

	gs_enable_framebuffer_srgb(linear_srgb);

	gs_blend_state_push();

	/* Colour blends normally, but alpha accumulates rather than being
	 * replaced, so stacking translucent elements into an empty target
	 * leaves the card's own edges correctly transparent. */
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	if (gs_texrender_begin(widget->composite, widget->width, widget->height)) {
		/* Binding a render target clears the flag, so it is set again
		 * for the composite pass and once more for the blit below. */
		gs_enable_framebuffer_srgb(linear_srgb);

		struct vec4 clear_color;
		vec4_zero(&clear_color);

		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)widget->width, 0.0f, (float)widget->height, -100.0f, 100.0f);

		draw_elements(widget);

		gs_texrender_end(widget->composite);
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(linear_srgb);

	gs_texture_t *current = gs_texrender_get_texture(widget->composite);
	if (!current) {
		gs_enable_framebuffer_srgb(previous_srgb);
		return;
	}

	ensure_previous_texture(widget);

	const bool transitioning = widget->transitioning && widget->previous_valid && widget->transition_seconds > 0.0f;

	float progress = 1.0f;
	if (transitioning)
		progress = std::min(widget->transition_time / widget->transition_seconds, 1.0f);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	const auto blit = [&](gs_texture_t *texture, float opacity, float offset_x) {
		DrawParams params;
		params.fill_mode = 2;
		params.texture = texture;
		params.x = offset_x + shown.dx;
		params.y = shown.dy;
		params.width = (float)widget->width;
		params.height = (float)widget->height;
		params.opacity = opacity * shown.opacity;
		params.scale = shown.scale;
		params.color = 0xFFFFFFFF;

		draw_quad(widget->effect, params, true);
	};

	if (!transitioning) {
		blit(current, 1.0f, 0.0f);
	} else {
		switch (widget->transition) {
		case TrackTransition::Slide: {
			const float travel = (float)widget->width;
			blit(widget->previous, 1.0f, -travel * progress);
			blit(current, 1.0f, travel * (1.0f - progress));
			break;
		}

		case TrackTransition::FadeThroughBlank:
			if (progress < 0.5f)
				blit(widget->previous, 1.0f - progress * 2.0f, 0.0f);
			else
				blit(current, (progress - 0.5f) * 2.0f, 0.0f);
			break;

		default:
			blit(widget->previous, 1.0f - progress, 0.0f);
			blit(current, progress, 0.0f);
			break;
		}
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);

	/* Hold on to a clean frame so the next track change has something to
	 * fade away from. */
	if (!transitioning && widget->previous) {
		gs_copy_texture(widget->previous, current);
		widget->previous_valid = true;
	}
}

void widget_enum_active_sources(void *data, obs_source_enum_proc_t callback, void *param)
{
	MusicWidget *widget = static_cast<MusicWidget *>(data);
	widget->bar.enum_active_sources(widget->self, callback, param);
}

} // namespace vr::widget
