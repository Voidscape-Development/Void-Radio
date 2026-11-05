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

/* OBS Studio 33 replaced the gs_image_file .. gs_image_file4 family with a
 * single gs_image_file_ex type (libnsgif 1.0 update), and the old functions
 * are no longer exported. This wraps both so the same sources build against
 * the OBS 31/32 SDK and the OBS 33+ SDK. A binary only runs on the OBS major
 * line it was built against, which is why OBS 33+ gets its own release files. */

#include <obs-config.h>
#include <graphics/image-file.h>

#if LIBOBS_API_MAJOR_VER >= 33

using vr_image_file_t = gs_image_file_ex_t;

static inline gs_image_file_ex_t &vr_image_file_base(vr_image_file_t &image)
{
	return image;
}

static inline const gs_image_file_ex_t &vr_image_file_base(const vr_image_file_t &image)
{
	return image;
}

static inline void vr_image_file_init(vr_image_file_t *image, const char *file, enum gs_image_alpha_mode alpha_mode)
{
	gs_image_file_ex_init(image, file, alpha_mode);
}

static inline void vr_image_file_free(vr_image_file_t *image)
{
	gs_image_file_ex_free(image);
}

static inline void vr_image_file_init_texture(vr_image_file_t *image)
{
	gs_image_file_ex_init_texture(image);
}

static inline bool vr_image_file_tick(vr_image_file_t *image, uint64_t elapsed_time_ns)
{
	return gs_image_file_ex_tick(image, elapsed_time_ns);
}

static inline void vr_image_file_update_texture(vr_image_file_t *image)
{
	gs_image_file_ex_update_texture(image);
}

#else

using vr_image_file_t = gs_image_file4_t;

static inline gs_image_file_t &vr_image_file_base(vr_image_file_t &image)
{
	return image.image3.image2.image;
}

static inline const gs_image_file_t &vr_image_file_base(const vr_image_file_t &image)
{
	return image.image3.image2.image;
}

static inline void vr_image_file_init(vr_image_file_t *image, const char *file, enum gs_image_alpha_mode alpha_mode)
{
	gs_image_file4_init(image, file, alpha_mode);
}

static inline void vr_image_file_free(vr_image_file_t *image)
{
	gs_image_file4_free(image);
}

static inline void vr_image_file_init_texture(vr_image_file_t *image)
{
	gs_image_file4_init_texture(image);
}

static inline bool vr_image_file_tick(vr_image_file_t *image, uint64_t elapsed_time_ns)
{
	return gs_image_file4_tick(image, elapsed_time_ns);
}

static inline void vr_image_file_update_texture(vr_image_file_t *image)
{
	gs_image_file4_update_texture(image);
}

#endif
