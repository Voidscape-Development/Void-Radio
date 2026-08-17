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

#include "render/text-render.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace vr {

namespace {

constexpr float PI = 3.14159265358979323846f;

/* A single element is never worth more texture than this; the numbers come
 * from the property sliders, which cannot ask for more. */
constexpr int MAX_TEXTURE_SIDE = 8192;

QColor from_obs_color(uint32_t color)
{
	/* OBS packs colour settings with red in the low byte. */
	return QColor((int)(color & 0xFF), (int)((color >> 8) & 0xFF), (int)((color >> 16) & 0xFF),
		      (int)((color >> 24) & 0xFF));
}

QString apply_transform(const QString &text, TextTransform transform)
{
	switch (transform) {
	case TextTransform::Upper:
		return text.toUpper();
	case TextTransform::Lower:
		return text.toLower();
	case TextTransform::Title: {
		QString result = text;
		bool boundary = true;

		for (int i = 0; i < result.size(); i++) {
			const QChar c = result.at(i);

			if (boundary)
				result[i] = c.toUpper();

			boundary = !c.isLetterOrNumber() && c != '\'';
		}

		return result;
	}
	default:
		return text;
	}
}

QFont build_font(const TextStyle &style, int size)
{
	QFont font(QString::fromStdString(style.font_family));

	font.setPixelSize(std::max(size, 1));
	font.setBold(style.bold);
	font.setItalic(style.italic);

	if (!style.font_style.empty())
		font.setStyleName(QString::fromStdString(style.font_style));

	if (std::abs(style.letter_spacing) > 0.001f)
		font.setLetterSpacing(QFont::AbsoluteSpacing, style.letter_spacing);

	return font;
}

/* Greedy word wrap. Words that are themselves too long are broken by
 * character, which is what a title made of one long word needs. */
QStringList wrap_line(const QString &line, const QFontMetricsF &metrics, double width, int max_lines)
{
	QStringList result;

	if (width <= 0.0) {
		result << line;
		return result;
	}

	QString current;

	const auto flush = [&]() {
		result << current;
		current.clear();
	};

	const QStringList words = line.split(' ');

	for (int i = 0; i < words.size(); i++) {
		QString word = words.at(i);
		const QString candidate = current.isEmpty() ? word : current + ' ' + word;

		if (metrics.horizontalAdvance(candidate) <= width) {
			current = candidate;
			continue;
		}

		if (!current.isEmpty()) {
			flush();

			if (max_lines > 0 && result.size() >= max_lines)
				break;
		}

		/* Break an oversized word across lines rather than letting it
		 * run off the side. */
		while (metrics.horizontalAdvance(word) > width && word.size() > 1) {
			int fit = 1;
			while (fit < word.size() && metrics.horizontalAdvance(word.left(fit + 1)) <= width)
				fit++;

			result << word.left(fit);
			word = word.mid(fit);

			if (max_lines > 0 && result.size() >= max_lines)
				break;
		}

		if (max_lines > 0 && result.size() >= max_lines)
			break;

		current = word;
	}

	if (!current.isEmpty() && (max_lines <= 0 || result.size() < max_lines))
		result << current;

	if (result.isEmpty())
		result << QString();

	return result;
}

Qt::TextElideMode elide_mode(EllipsisSide side)
{
	switch (side) {
	case EllipsisSide::Left:
		return Qt::ElideLeft;
	case EllipsisSide::Middle:
		return Qt::ElideMiddle;
	default:
		return Qt::ElideRight;
	}
}

/*
 * Blurs the alpha channel only, three box passes deep, which is close enough
 * to a gaussian for a drop shadow. The colour channels are left alone because
 * the shadow is a flat colour applied afterwards; blurring them as well would
 * drag the edges of a coloured shadow towards black.
 */
void blur_alpha(QImage &image, int radius)
{
	if (image.isNull() || radius < 1)
		return;

	const int width = image.width();
	const int height = image.height();
	const int window = radius * 2 + 1;

	std::vector<uint8_t> scratch((size_t)width * height);

	const auto pass = [&](bool horizontal) {
		const int outer = horizontal ? height : width;
		const int inner = horizontal ? width : height;

		for (int o = 0; o < outer; o++) {
			const auto sample = [&](int i) -> int {
				const int clamped = std::min(std::max(i, 0), inner - 1);
				const int x = horizontal ? clamped : o;
				const int y = horizontal ? o : clamped;
				return image.constScanLine(y)[x * 4 + 3];
			};

			int total = 0;
			for (int i = -radius; i <= radius; i++)
				total += sample(i);

			for (int i = 0; i < inner; i++) {
				const int x = horizontal ? i : o;
				const int y = horizontal ? o : i;

				scratch[(size_t)y * width + x] = (uint8_t)(total / window);
				total += sample(i + radius + 1) - sample(i - radius);
			}
		}

		for (int y = 0; y < height; y++) {
			uint8_t *line = image.scanLine(y);

			for (int x = 0; x < width; x++)
				line[x * 4 + 3] = scratch[(size_t)y * width + x];
		}
	};

	for (int i = 0; i < 3; i++) {
		pass(true);
		pass(false);
	}
}

bool image_to_rgba(const QImage &image, RgbaImage &out)
{
	const QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
	if (converted.isNull())
		return false;

	out.width = (uint32_t)converted.width();
	out.height = (uint32_t)converted.height();
	out.pixels.resize((size_t)out.width * out.height * 4);

	const size_t stride = (size_t)out.width * 4;

	for (uint32_t y = 0; y < out.height; y++)
		memcpy(out.pixels.data() + (size_t)y * stride, converted.constScanLine((int)y), stride);

	return out.valid();
}

} // namespace

bool TextStyle::operator==(const TextStyle &other) const
{
	return font_family == other.font_family && font_style == other.font_style && font_size == other.font_size &&
	       bold == other.bold && italic == other.italic && underline == other.underline && color == other.color &&
	       color2 == other.color2 && gradient == other.gradient && gradient_angle == other.gradient_angle &&
	       outline_width == other.outline_width && outline_color == other.outline_color && shadow == other.shadow &&
	       shadow_x == other.shadow_x && shadow_y == other.shadow_y && shadow_blur == other.shadow_blur &&
	       shadow_color == other.shadow_color && letter_spacing == other.letter_spacing &&
	       line_height == other.line_height && transform == other.transform && align == other.align &&
	       valign == other.valign && overflow == other.overflow && marquee_style == other.marquee_style &&
	       ellipsis_side == other.ellipsis_side && max_lines == other.max_lines && shrink_min == other.shrink_min &&
	       marquee_gap == other.marquee_gap && marquee_speed == other.marquee_speed &&
	       marquee_pause == other.marquee_pause;
}

bool rasterize_text(const std::string &utf8, const TextStyle &style, uint32_t box_width, uint32_t box_height,
		    RasterizedText &out)
{
	out = RasterizedText();

	if (utf8.empty() || box_width == 0 || box_height == 0)
		return false;

	const QString source = apply_transform(QString::fromStdString(utf8), style.transform);
	if (source.isEmpty())
		return false;

	/* Explicit newlines in a template are always honoured; wrapping only
	 * ever adds further breaks. */
	QStringList paragraphs = source.split('\n');

	int size = std::max(style.font_size, 1);
	QFont font = build_font(style, size);
	QFontMetricsF metrics(font);

	QStringList lines;
	bool scrolls = false;

	const double available = (double)box_width;

	switch (style.overflow) {
	case TextOverflow::Wrap: {
		const int limit = std::max(style.max_lines, 1);

		for (const QString &paragraph : paragraphs) {
			const QStringList wrapped = wrap_line(paragraph, metrics, available, limit - lines.size());

			for (const QString &line : wrapped) {
				if (lines.size() >= limit)
					break;
				lines << line;
			}

			if (lines.size() >= limit)
				break;
		}

		/* Anything that still did not fit is signalled on the last
		 * line rather than silently dropped. */
		if (!lines.isEmpty() && metrics.horizontalAdvance(lines.last()) > available)
			lines.last() = metrics.elidedText(lines.last(), Qt::ElideRight, available);

		break;
	}

	case TextOverflow::Shrink: {
		const int floor_size = std::max(1, (int)std::lround(size * std::max(style.shrink_min, 1.0f) / 100.0f));

		while (size > floor_size) {
			double widest = 0.0;
			for (const QString &paragraph : paragraphs)
				widest = std::max(widest, metrics.horizontalAdvance(paragraph));

			const double total_height = metrics.height() * style.line_height * paragraphs.size();

			if (widest <= available && total_height <= (double)box_height)
				break;

			size--;
			font = build_font(style, size);
			metrics = QFontMetricsF(font);
		}

		lines = paragraphs;
		break;
	}

	case TextOverflow::Ellipsis: {
		for (const QString &paragraph : paragraphs)
			lines << metrics.elidedText(paragraph, elide_mode(style.ellipsis_side), available);
		break;
	}

	case TextOverflow::Marquee: {
		/* Scrolling only makes sense for one line, so a template with
		 * newlines is flattened before measuring. */
		const QString flattened = paragraphs.join(QStringLiteral("  "));
		lines << flattened;
		scrolls = metrics.horizontalAdvance(flattened) > available;
		break;
	}

	default:
		lines = paragraphs;
		break;
	}

	if (lines.isEmpty())
		return false;

	double content_width = 0.0;
	for (const QString &line : lines)
		content_width = std::max(content_width, metrics.horizontalAdvance(line));

	const double line_step = metrics.height() * std::max(style.line_height, 0.1f);
	const double content_height = line_step * (lines.size() - 1) + metrics.height();

	/* Room for the outline, and for however far the shadow is thrown. */
	int pad = (int)std::ceil(style.outline_width) + 2;

	if (style.shadow) {
		const double reach = std::max(std::abs(style.shadow_x), std::abs(style.shadow_y)) + style.shadow_blur;
		pad = std::max(pad, (int)std::ceil(reach) + 2);
	}

	const double gap = scrolls ? std::max((double)style.marquee_gap, (double)pad * 2.0 + 8.0) : 0.0;

	int canvas_width;
	int canvas_height;

	if (scrolls) {
		/* One copy plus the gap: the whole texture is the scroll
		 * period, and wrapped sampling makes it repeat. */
		canvas_width = (int)std::ceil(pad + content_width + gap);
		canvas_height = (int)std::ceil(std::max((double)box_height, content_height)) + pad * 2;
	} else {
		canvas_width = (int)box_width + pad * 2;
		canvas_height = (int)box_height + pad * 2;
	}

	canvas_width = std::min(std::max(canvas_width, 1), MAX_TEXTURE_SIDE);
	canvas_height = std::min(std::max(canvas_height, 1), MAX_TEXTURE_SIDE);

	/* Painted premultiplied, which is the format Qt's raster engine is
	 * fastest and most accurate in; image_to_rgba undoes it at the end. */
	QImage canvas(canvas_width, canvas_height, QImage::Format_RGBA8888_Premultiplied);
	canvas.fill(Qt::transparent);

	/* Build the glyph outlines once; the shadow, the outline and the fill
	 * are all drawn from the same path. */
	QPainterPath path;

	double y = pad;

	if (!scrolls) {
		const double slack = (double)box_height - content_height;

		if (style.valign == TextVAlign::Middle)
			y += std::max(slack, 0.0) * 0.5;
		else if (style.valign == TextVAlign::Bottom)
			y += std::max(slack, 0.0);
	} else {
		y += std::max((canvas_height - pad * 2 - content_height), 0.0) * 0.5;
	}

	const double baseline_offset = metrics.ascent();

	for (const QString &line : lines) {
		const double line_width = metrics.horizontalAdvance(line);

		double x = pad;

		if (!scrolls) {
			const double slack = (double)box_width - line_width;

			if (style.align == TextAlign::Center)
				x += slack * 0.5;
			else if (style.align == TextAlign::Right)
				x += slack;
		}

		path.addText((qreal)x, (qreal)(y + baseline_offset), font, line);

		if (style.underline) {
			const double thickness = std::max(metrics.lineWidth(), 1.0);
			const double position = y + baseline_offset + metrics.underlinePos();
			path.addRect((qreal)x, (qreal)position, (qreal)line_width, (qreal)thickness);
		}

		y += line_step;
	}

	if (style.shadow) {
		/* The silhouette is drawn opaque, blurred, and only then tinted,
		 * so the blur cannot drag a coloured shadow towards black. */
		QImage shadow(canvas_width, canvas_height, QImage::Format_RGBA8888_Premultiplied);
		shadow.fill(Qt::transparent);

		{
			QPainter painter(&shadow);
			painter.setRenderHint(QPainter::Antialiasing, true);
			painter.translate(style.shadow_x, style.shadow_y);

			if (style.outline_width > 0.0f) {
				QPen pen(Qt::white, style.outline_width * 2.0);
				pen.setJoinStyle(Qt::RoundJoin);
				pen.setCapStyle(Qt::RoundCap);
				painter.strokePath(path, pen);
			}

			painter.fillPath(path, Qt::white);
		}

		if (style.shadow_blur >= 1.0f)
			blur_alpha(shadow, (int)std::lround(style.shadow_blur));

		const QColor tint = from_obs_color(style.shadow_color);

		for (int row = 0; row < shadow.height(); row++) {
			uint8_t *line = shadow.scanLine(row);

			for (int column = 0; column < shadow.width(); column++) {
				uint8_t *pixel = line + column * 4;
				const int alpha = pixel[3] * tint.alpha() / 255;

				/* Still premultiplied at this point. */
				pixel[0] = (uint8_t)(tint.red() * alpha / 255);
				pixel[1] = (uint8_t)(tint.green() * alpha / 255);
				pixel[2] = (uint8_t)(tint.blue() * alpha / 255);
				pixel[3] = (uint8_t)alpha;
			}
		}

		QPainter painter(&canvas);
		painter.drawImage(0, 0, shadow);
	}

	{
		QPainter painter(&canvas);
		painter.setRenderHint(QPainter::Antialiasing, true);

		if (style.outline_width > 0.0f) {
			/* A centred stroke of twice the width leaves exactly the
			 * requested amount outside the glyph. */
			QPen pen(from_obs_color(style.outline_color), style.outline_width * 2.0);
			pen.setJoinStyle(Qt::RoundJoin);
			pen.setCapStyle(Qt::RoundCap);
			painter.strokePath(path, pen);
		}

		if (style.gradient) {
			const QRectF bounds = path.boundingRect();
			const double radians = style.gradient_angle * PI / 180.0;
			const double dx = std::cos(radians) * bounds.width() * 0.5;
			const double dy = -std::sin(radians) * bounds.height() * 0.5;

			QLinearGradient gradient(bounds.center().x() - dx, bounds.center().y() - dy,
						 bounds.center().x() + dx, bounds.center().y() + dy);

			gradient.setColorAt(0.0, from_obs_color(style.color));
			gradient.setColorAt(1.0, from_obs_color(style.color2));

			painter.fillPath(path, gradient);
		} else {
			painter.fillPath(path, from_obs_color(style.color));
		}
	}

	if (!image_to_rgba(canvas, out.image))
		return false;

	out.pad = pad;
	out.content_width = (uint32_t)std::lround(content_width);
	out.content_height = (uint32_t)std::lround(content_height);
	out.scrolls = scrolls;

	return true;
}

float marquee_offset(const RasterizedText &text, const TextStyle &style, uint32_t box_width, float elapsed)
{
	if (!text.scrolls || !text.valid())
		return 0.0f;

	const float speed = std::max(style.marquee_speed, 1.0f);

	if (style.marquee_style == MarqueeStyle::Loop) {
		/* The texture is exactly one period wide, so the offset simply
		 * wraps around it. */
		const float period = (float)text.image.width;
		if (period <= 0.0f)
			return 0.0f;

		return std::fmod(elapsed * speed, period);
	}

	/* Bounce: out, hold, back, hold. */
	const float travel = std::max((float)text.content_width + text.pad * 2.0f - (float)box_width, 0.0f);
	if (travel <= 0.0f)
		return 0.0f;

	const float pause = std::max(style.marquee_pause, 0.0f);
	const float leg = travel / speed;
	const float cycle = (leg + pause) * 2.0f;

	if (cycle <= 0.0f)
		return 0.0f;

	float t = std::fmod(elapsed, cycle);

	if (t < leg)
		return t * speed;

	t -= leg;

	if (t < pause)
		return travel;

	t -= pause;

	if (t < leg)
		return travel - t * speed;

	return 0.0f;
}

} // namespace vr
