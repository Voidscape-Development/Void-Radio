#include "media-fixtures.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixfmt.h>
}

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace fixture {

namespace {

bool fail(std::string *error, const std::string &what, int code = 0)
{
	if (error) {
		*error = what;
		if (code) {
			char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
			av_strerror(code, buffer, sizeof(buffer));
			*error += std::string(": ") + buffer;
		}
	}
	return false;
}

AVSampleFormat pick_format(const AVCodec *codec)
{
	static const AVSampleFormat preferred[] = {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_FLTP,
						   AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32,  AV_SAMPLE_FMT_S32P};

	const AVSampleFormat *formats = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
	const void *list = nullptr;
	int count = 0;
	if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &list, &count) >= 0)
		formats = static_cast<const AVSampleFormat *>(list);
#else
	formats = codec->sample_fmts;
#endif

	if (!formats)
		return AV_SAMPLE_FMT_S16;

	for (AVSampleFormat want : preferred)
		for (const AVSampleFormat *f = formats; *f != AV_SAMPLE_FMT_NONE; f++)
			if (*f == want)
				return want;

	return formats[0];
}

void fill(AVFrame *frame, int64_t first_sample)
{
	const int channels = frame->ch_layout.nb_channels;
	const bool planar = av_sample_fmt_is_planar((AVSampleFormat)frame->format);

	for (int i = 0; i < frame->nb_samples; i++) {
		const int value = (int)((first_sample + i) % SAW_PERIOD) - SAW_OFFSET;

		for (int ch = 0; ch < channels; ch++) {
			const int plane = planar ? ch : 0;
			const int index = planar ? i : i * channels + ch;
			uint8_t *base = frame->extended_data[plane];

			switch (frame->format) {
			case AV_SAMPLE_FMT_S16:
			case AV_SAMPLE_FMT_S16P:
				reinterpret_cast<int16_t *>(base)[index] = (int16_t)value;
				break;
			case AV_SAMPLE_FMT_S32:
			case AV_SAMPLE_FMT_S32P:
				reinterpret_cast<int32_t *>(base)[index] = value * 65536;
				break;
			default:
				reinterpret_cast<float *>(base)[index] = (float)value / 32768.0f;
				break;
			}
		}
	}
}

bool encode(AVFormatContext *oc, AVCodecContext *ctx, AVStream *st, AVFrame *frame, AVPacket *pkt, std::string *error)
{
	int ret = avcodec_send_frame(ctx, frame);
	if (ret < 0)
		return fail(error, "send_frame", ret);

	for (;;) {
		ret = avcodec_receive_packet(ctx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return true;
		if (ret < 0)
			return fail(error, "receive_packet", ret);

		av_packet_rescale_ts(pkt, ctx->time_base, st->time_base);
		pkt->stream_index = st->index;

		ret = av_interleaved_write_frame(oc, pkt);
		if (ret < 0)
			return fail(error, "write_frame", ret);
	}
}

} // namespace

std::vector<unsigned char> picture_bytes(size_t index)
{
	/* A real 1x1 PNG whose one pixel's red channel is the index, encoded by
	 * FFmpeg so that it is valid down to the checksums. */
	std::vector<unsigned char> bytes;

	const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
	AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
	AVFrame *frame = av_frame_alloc();
	AVPacket *pkt = av_packet_alloc();

	if (ctx && frame && pkt) {
		ctx->width = 1;
		ctx->height = 1;
		ctx->pix_fmt = AV_PIX_FMT_RGB24;
		ctx->time_base = AVRational{1, 25};

		frame->width = 1;
		frame->height = 1;
		frame->format = AV_PIX_FMT_RGB24;

		if (avcodec_open2(ctx, codec, nullptr) >= 0 && av_frame_get_buffer(frame, 0) >= 0) {
			frame->data[0][0] = (uint8_t)index;
			frame->data[0][1] = 0x40;
			frame->data[0][2] = 0x80;

			if (avcodec_send_frame(ctx, frame) >= 0 && avcodec_receive_packet(ctx, pkt) >= 0)
				bytes.assign(pkt->data, pkt->data + pkt->size);
		}
	}

	av_packet_free(&pkt);
	av_frame_free(&frame);
	avcodec_free_context(&ctx);
	return bytes;
}

int saw_position(float sample)
{
	return (int)std::lround(sample * 32768.0f) + SAW_OFFSET;
}

std::string temp_path(const std::string &name)
{
	const char *dir = getenv("TMPDIR");
	return std::string(dir && *dir ? dir : "/tmp") + "/void-radio-test-" + name;
}

bool supported(const Spec &spec)
{
	return avcodec_find_encoder_by_name(spec.codec.c_str()) &&
	       av_guess_format(spec.format.c_str(), nullptr, nullptr);
}

bool write(const std::string &path, const Spec &spec, std::string *error)
{
	av_log_set_level(AV_LOG_ERROR);

	const AVCodec *codec = avcodec_find_encoder_by_name(spec.codec.c_str());
	if (!codec)
		return fail(error, "no encoder " + spec.codec);

	AVFormatContext *oc = nullptr;
	int ret = avformat_alloc_output_context2(&oc, nullptr, spec.format.c_str(), path.c_str());
	if (ret < 0 || !oc)
		return fail(error, "alloc output", ret);

	bool ok = false;
	AVCodecContext *ctx = nullptr;
	AVFrame *frame = nullptr;
	AVPacket *pkt = av_packet_alloc();

	do {
		AVStream *st = avformat_new_stream(oc, nullptr);
		ctx = avcodec_alloc_context3(codec);
		if (!st || !ctx || !pkt) {
			fail(error, "alloc");
			break;
		}

		ctx->sample_rate = spec.sample_rate;
		av_channel_layout_default(&ctx->ch_layout, spec.channels);
		ctx->sample_fmt = pick_format(codec);
		ctx->time_base = AVRational{1, spec.sample_rate};
		ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
		if (oc->oformat->flags & AVFMT_GLOBALHEADER)
			ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		ret = avcodec_open2(ctx, codec, nullptr);
		if (ret < 0) {
			fail(error, "open encoder", ret);
			break;
		}

		avcodec_parameters_from_context(st->codecpar, ctx);
		st->time_base = ctx->time_base;

		for (const auto &item : spec.metadata) {
			av_dict_set(&oc->metadata, item.first.c_str(), item.second.c_str(), 0);
			/* Ogg keeps its comments on the stream. */
			av_dict_set(&st->metadata, item.first.c_str(), item.second.c_str(), 0);
		}

		std::vector<AVStream *> pics;
		for (const std::string &comment : spec.pictures) {
			AVStream *pic = avformat_new_stream(oc, nullptr);
			pic->disposition = AV_DISPOSITION_ATTACHED_PIC;
			pic->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
			pic->codecpar->codec_id = AV_CODEC_ID_PNG;
			pic->codecpar->width = 1;
			pic->codecpar->height = 1;
			pic->time_base = AVRational{1, 90000};
			av_dict_set(&pic->metadata, "comment", comment.c_str(), 0);
			pics.push_back(pic);
		}

		if (!(oc->oformat->flags & AVFMT_NOFILE)) {
			ret = avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE);
			if (ret < 0) {
				fail(error, "open file", ret);
				break;
			}
		}

		AVDictionary *options = nullptr;
		for (const auto &item : spec.muxer_options)
			av_dict_set(&options, item.first.c_str(), item.second.c_str(), 0);

		ret = avformat_write_header(oc, &options);
		av_dict_free(&options);
		if (ret < 0) {
			fail(error, "write header", ret);
			break;
		}

		bool pictures_ok = true;
		for (size_t i = 0; i < pics.size() && pictures_ok; i++) {
			const std::vector<unsigned char> bytes = picture_bytes(i);
			AVPacket *art = av_packet_alloc();
			av_new_packet(art, (int)bytes.size());
			memcpy(art->data, bytes.data(), bytes.size());
			art->stream_index = pics[i]->index;
			art->flags |= AV_PKT_FLAG_KEY;
			ret = av_interleaved_write_frame(oc, art);
			av_packet_free(&art);
			if (ret < 0)
				pictures_ok = fail(error, "write picture", ret);
		}
		if (!pictures_ok)
			break;

		frame = av_frame_alloc();
		frame->format = ctx->sample_fmt;
		frame->sample_rate = ctx->sample_rate;
		av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);

		const int frame_size = ctx->frame_size > 0 ? ctx->frame_size : 1024;
		const int64_t total = (int64_t)spec.duration_ms * spec.sample_rate / 1000;
		bool failed = false;

		for (int64_t done = 0; done < total && !failed; done += frame_size) {
			frame->nb_samples = (int)std::min<int64_t>(frame_size, total - done);
			if (ctx->frame_size > 0 &&
			    !(codec->capabilities & (AV_CODEC_CAP_VARIABLE_FRAME_SIZE | AV_CODEC_CAP_SMALL_LAST_FRAME)))
				frame->nb_samples = frame_size;

			av_frame_get_buffer(frame, 0);
			av_frame_make_writable(frame);
			fill(frame, done);
			frame->pts = done;

			failed = !encode(oc, ctx, st, frame, pkt, error);
			av_frame_unref(frame);
			frame->format = ctx->sample_fmt;
			frame->sample_rate = ctx->sample_rate;
			av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);
		}

		if (failed || !encode(oc, ctx, st, nullptr, pkt, error))
			break;

		ret = av_write_trailer(oc);
		if (ret < 0) {
			fail(error, "write trailer", ret);
			break;
		}

		ok = true;
	} while (false);

	av_frame_free(&frame);
	av_packet_free(&pkt);
	avcodec_free_context(&ctx);
	if (oc && !(oc->oformat->flags & AVFMT_NOFILE))
		avio_closep(&oc->pb);
	avformat_free_context(oc);

	return ok;
}

} // namespace fixture
