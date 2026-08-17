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

#include "render/image-util.hpp"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vr {

namespace {

/* Anything larger is scaled down before analysis; a cover is never shown at
 * more than a few hundred pixels and the extra detail only costs time. */
constexpr int ANALYSIS_SIZE = 64;

constexpr int HUE_BUCKETS = 24;

QImage to_qimage(const RgbaImage &source)
{
	if (!source.valid())
		return QImage();

	/* Wraps the caller's buffer, then copies so the QImage owns its data. */
	const QImage view(source.pixels.data(), (int)source.width, (int)source.height, (int)source.width * 4,
			  QImage::Format_RGBA8888);

	return view.copy();
}

bool from_qimage(const QImage &image, RgbaImage &out)
{
	if (image.isNull())
		return false;

	const QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
	if (converted.isNull())
		return false;

	out.width = (uint32_t)converted.width();
	out.height = (uint32_t)converted.height();
	out.pixels.resize((size_t)out.width * out.height * 4);

	const int stride = (int)out.width * 4;

	for (uint32_t y = 0; y < out.height; y++)
		memcpy(out.pixels.data() + (size_t)y * stride, converted.constScanLine((int)y), (size_t)stride);

	return out.valid();
}

QImage limit_size(QImage image, uint32_t max_dimension)
{
	if (image.isNull() || max_dimension == 0)
		return image;

	const int longest = std::max(image.width(), image.height());
	if (longest <= (int)max_dimension)
		return image;

	return image.scaled((int)max_dimension, (int)max_dimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

/* Crops to the target aspect ratio, then scales, so the image fills the box
 * without distortion. */
QImage cover_scale(const QImage &image, int width, int height)
{
	if (image.isNull() || width <= 0 || height <= 0)
		return QImage();

	const double source_aspect = (double)image.width() / (double)image.height();
	const double target_aspect = (double)width / (double)height;

	QRect crop(0, 0, image.width(), image.height());

	if (source_aspect > target_aspect) {
		const int cropped = (int)std::lround(image.height() * target_aspect);
		crop = QRect((image.width() - cropped) / 2, 0, std::max(cropped, 1), image.height());
	} else if (source_aspect < target_aspect) {
		const int cropped = (int)std::lround(image.width() / target_aspect);
		crop = QRect(0, (image.height() - cropped) / 2, image.width(), std::max(cropped, 1));
	}

	return image.copy(crop).scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

/* Three box passes approximate a gaussian closely enough for a backdrop. */
void box_blur(QImage &image, int radius)
{
	if (image.isNull() || radius < 1)
		return;

	image = image.convertToFormat(QImage::Format_RGBA8888);

	const int width = image.width();
	const int height = image.height();
	const int window = radius * 2 + 1;

	std::vector<uint8_t> scratch((size_t)width * height * 4);

	const auto pass = [&](bool horizontal) {
		const int outer = horizontal ? height : width;
		const int inner = horizontal ? width : height;

		for (int o = 0; o < outer; o++) {
			int totals[4] = {0, 0, 0, 0};

			const auto sample = [&](int i, int channel) -> int {
				const int clamped = std::min(std::max(i, 0), inner - 1);
				const int x = horizontal ? clamped : o;
				const int y = horizontal ? o : clamped;
				return image.constScanLine(y)[x * 4 + channel];
			};

			for (int i = -radius; i <= radius; i++) {
				for (int c = 0; c < 4; c++)
					totals[c] += sample(i, c);
			}

			for (int i = 0; i < inner; i++) {
				const int x = horizontal ? i : o;
				const int y = horizontal ? o : i;
				uint8_t *dest = scratch.data() + ((size_t)y * width + x) * 4;

				for (int c = 0; c < 4; c++) {
					dest[c] = (uint8_t)(totals[c] / window);
					totals[c] += sample(i + radius + 1, c) - sample(i - radius, c);
				}
			}
		}

		for (int y = 0; y < height; y++)
			memcpy(image.scanLine(y), scratch.data() + (size_t)y * width * 4, (size_t)width * 4);
	};

	for (int i = 0; i < 3; i++) {
		pass(true);
		pass(false);
	}
}

} // namespace

bool decode_image(const uint8_t *data, size_t size, RgbaImage &out, uint32_t max_dimension)
{
	out.clear();

	if (!data || size == 0)
		return false;

	QImage image;
	if (!image.loadFromData(data, (int)size))
		return false;

	return from_qimage(limit_size(std::move(image), max_dimension), out);
}

bool decode_image_file(const std::string &path, RgbaImage &out, uint32_t max_dimension)
{
	out.clear();

	if (path.empty())
		return false;

	QImage image;
	if (!image.load(QString::fromStdString(path)))
		return false;

	return from_qimage(limit_size(std::move(image), max_dimension), out);
}

bool blur_image(const RgbaImage &source, uint32_t width, uint32_t height, float strength, float darken,
		float desaturate, RgbaImage &out)
{
	out.clear();

	if (!source.valid() || width == 0 || height == 0)
		return false;

	const QImage original = to_qimage(source);
	if (original.isNull())
		return false;

	strength = std::min(std::max(strength, 0.0f), 1.0f);

	/* Most of the blur comes from throwing detail away and letting the
	 * smooth upscale put it back; the box passes just take the edge off
	 * whatever survives. */
	const float scale = 1.0f - strength * 0.94f;
	const int small_width = std::max(2, (int)std::lround(width * scale));
	const int small_height = std::max(2, (int)std::lround(height * scale));

	QImage working = cover_scale(original, small_width, small_height);
	if (working.isNull())
		return false;

	box_blur(working, std::max(1, (int)std::lround(strength * 6.0f)));

	QImage result = working.scaled((int)width, (int)height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
				.convertToFormat(QImage::Format_RGBA8888);

	darken = std::min(std::max(darken, 0.0f), 1.0f);
	desaturate = std::min(std::max(desaturate, 0.0f), 1.0f);

	if (darken > 0.0f || desaturate > 0.0f) {
		for (int y = 0; y < result.height(); y++) {
			uint8_t *line = result.scanLine(y);

			for (int x = 0; x < result.width(); x++) {
				uint8_t *pixel = line + x * 4;

				float r = pixel[0];
				float g = pixel[1];
				float b = pixel[2];

				if (desaturate > 0.0f) {
					const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
					r += (luma - r) * desaturate;
					g += (luma - g) * desaturate;
					b += (luma - b) * desaturate;
				}

				const float keep = 1.0f - darken;

				pixel[0] = (uint8_t)std::lround(r * keep);
				pixel[1] = (uint8_t)std::lround(g * keep);
				pixel[2] = (uint8_t)std::lround(b * keep);
			}
		}
	}

	return from_qimage(result, out);
}

uint32_t dominant_color(const RgbaImage &source)
{
	if (!source.valid())
		return 0;

	const QImage original = to_qimage(source);
	if (original.isNull())
		return 0;

	const QImage small =
		original.scaled(ANALYSIS_SIZE, ANALYSIS_SIZE, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
			.convertToFormat(QImage::Format_RGBA8888);

	if (small.isNull())
		return 0;

	/* Weight by saturation so that a mostly grey cover with one bright
	 * detail still yields something worth tinting with, and by a mid
	 * biased value so neither the shadows nor the blown highlights win. */
	double weights[HUE_BUCKETS] = {};
	double reds[HUE_BUCKETS] = {};
	double greens[HUE_BUCKETS] = {};
	double blues[HUE_BUCKETS] = {};

	double grey_weight = 0.0;
	double grey_r = 0.0;
	double grey_g = 0.0;
	double grey_b = 0.0;

	for (int y = 0; y < small.height(); y++) {
		const uint8_t *line = small.constScanLine(y);

		for (int x = 0; x < small.width(); x++) {
			const uint8_t *pixel = line + x * 4;

			if (pixel[3] < 16)
				continue;

			const QColor color = QColor::fromRgb(pixel[0], pixel[1], pixel[2]);

			int hue = 0;
			int saturation = 0;
			int value = 0;
			color.getHsv(&hue, &saturation, &value);

			const double value_weight = 1.0 - std::abs(value - 160) / 220.0;
			const double weight = std::max(value_weight, 0.05) * (saturation / 255.0);

			grey_weight += 1.0;
			grey_r += pixel[0];
			grey_g += pixel[1];
			grey_b += pixel[2];

			if (hue < 0 || saturation < 40 || weight <= 0.0)
				continue;

			const int bucket = std::min(hue * HUE_BUCKETS / 360, HUE_BUCKETS - 1);

			weights[bucket] += weight;
			reds[bucket] += pixel[0] * weight;
			greens[bucket] += pixel[1] * weight;
			blues[bucket] += pixel[2] * weight;
		}
	}

	int best = -1;
	for (int i = 0; i < HUE_BUCKETS; i++) {
		if (best < 0 || weights[i] > weights[best])
			best = i;
	}

	double r;
	double g;
	double b;

	if (best >= 0 && weights[best] > 0.0) {
		r = reds[best] / weights[best];
		g = greens[best] / weights[best];
		b = blues[best] / weights[best];
	} else if (grey_weight > 0.0) {
		/* A genuinely colourless cover; the average is the honest
		 * answer even though it will not be vivid. */
		r = grey_r / grey_weight;
		g = grey_g / grey_weight;
		b = grey_b / grey_weight;
	} else {
		return 0;
	}

	QColor accent = QColor::fromRgb((int)std::lround(r), (int)std::lround(g), (int)std::lround(b));

	int hue = 0;
	int saturation = 0;
	int value = 0;
	accent.getHsv(&hue, &saturation, &value);

	/* Pushed towards something that still reads as a highlight when it lands
	 * on a bar fill or a border. A hue of -1 means the colour is a pure
	 * grey, which has nothing to push. */
	if (hue >= 0) {
		saturation = std::min(255, (int)std::lround(saturation * 1.25) + 20);
		value = std::min(240, std::max(140, value));

		accent = QColor::fromHsv(hue, saturation, value);
	}

	/* OBS packs colour settings with red in the low byte. */
	return 0xFF000000u | ((uint32_t)accent.blue() << 16) | ((uint32_t)accent.green() << 8) | (uint32_t)accent.red();
}

} // namespace vr
