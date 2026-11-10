/*
 * Tests for the tones worked out from the album colour: the dark tone is
 * darker and the light one lighter, the hue survives, alpha is kept, and the
 * contrast colour is always legible on the accent.
 */

#include "sources/music-widget/widget-palette.hpp"
#include "test-check.hpp"

#include <cstdio>

using namespace vr::widget;
using test::check;
using test::check_true;

int main()
{
	/* 0xAABBGGRR. */
	const uint32_t samples[] = {
		0xFF3C14DC, /* crimson */
		0xFF00D7FF, /* gold */
		0xFFE16941, /* royal blue */
		0xFF7FFF00, /* chartreuse */
		0xFF808080, /* grey */
		0xFFFFFFFF, /* white */
		0xFF000000, /* black */
	};

	for (uint32_t accent : samples) {
		char label[64];
		const Palette palette = derive_palette(accent);

		std::snprintf(label, sizeof(label), "%08X: accent kept", accent);
		check(label, (long long)palette.accent, (long long)accent);

		/* The dark tone carries white text and the light one dark text,
		 * at WCAG's AAA ratio for body text. */
		std::snprintf(label, sizeof(label), "%08X: dark is dark", accent);
		check_true(label, relative_luminance(palette.dark) <= relative_luminance(accent) + 1e-4f &&
					  contrast_ratio(palette.dark, 0xFFFFFFFF) >= 7.0f);

		std::snprintf(label, sizeof(label), "%08X: light is light", accent);
		check_true(label, relative_luminance(palette.light) >= relative_luminance(accent) - 1e-4f &&
					  contrast_ratio(palette.light, 0xFF141414) >= 7.0f);

		/* Whichever of black and white is picked is the better of the
		 * two, which is always at least 4.5:1 against any colour. */
		std::snprintf(label, sizeof(label), "%08X: contrast legible", accent);
		check_true(label, contrast_ratio(palette.contrast, accent) >= 4.5f);
	}

	/* White on black is the full 21:1. */
	{
		const float ratio = contrast_ratio(0xFFFFFFFF, 0xFF000000);
		check_true("contrast: white on black is 21", ratio > 20.9f && ratio < 21.1f);
	}

	/* Alpha passes through every tone. */
	{
		const Palette palette = derive_palette(0x803C14DC);
		check("alpha: dark", (long long)(palette.dark >> 24), 0x80);
		check("alpha: light", (long long)(palette.light >> 24), 0x80);
		check("alpha: contrast", (long long)(palette.contrast >> 24), 0x80);
	}

	/* A saturated colour keeps its hue in its dark tone: crimson stays red. */
	{
		const Palette palette = derive_palette(0xFF3C14DC);
		const uint32_t r = palette.dark & 0xFF;
		const uint32_t b = (palette.dark >> 16) & 0xFF;
		check_true("hue: dark crimson is still red", r > b);
	}

	check("tone: accent", (long long)palette_tone(derive_palette(0xFF3C14DC), AccentTone::Accent), 0xFF3C14DCLL);

	return test::finish();
}
