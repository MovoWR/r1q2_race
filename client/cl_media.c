/*
 * Private cinematic decoding based on Q2PRO src/client/cin.c,
 * revision 601a8df8433b0c50dbbe37c0716c3793fff140a7.
 * Copyright (C) 2023 Andrey Nazarov
 * GPL version 2 or (at your option) later. WITHOUT WARRANTY. See COPYING.
 */
#ifndef _WIN32
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif
#include "cl_media.h"
#include <string.h>

static void CL_MediaMessage (char *out, int size, const char *text)
{
	if (out && size > 0)
	{
		strncpy (out, text, size - 1);
		out[size - 1] = 0;
	}
}

#ifdef USE_AVCODEC
#include <stdint.h>
#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 37, 100) ||  \
	LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 27, 100) || \
	LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 28, 100) ||   \
	LIBSWRESAMPLE_VERSION_INT < AV_VERSION_INT(4, 7, 100) || \
	LIBSWSCALE_VERSION_INT < AV_VERSION_INT(6, 7, 100)
#error Unsupported FFmpeg version for cinematics
#endif

#ifdef _WIN32
#define CL_MediaSeek _fseeki64
#define CL_MediaTell _ftelli64
#else
#define CL_MediaSeek fseeko
#define CL_MediaTell ftello
#endif

#define MEDIA_MAX_AXIS 8192
#define MEDIA_AUDIO_FRAMES 4096
#define MEDIA_MAX_CHANNELS 64
#define MEDIA_MAX_SAMPLES 1048576
#define MEDIA_PACKET_BYTES (16 * 1024 * 1024)
#define MEDIA_STEPS 128
#ifndef MEDIA_QUEUE_BYTES
#define MEDIA_QUEUE_BYTES (32 * 1024 * 1024)
#endif
#ifndef MEDIA_QUEUE_PACKETS
#define MEDIA_QUEUE_PACKETS 2048
#endif

typedef struct
{
	const char *extension, *demuxer;
	enum AVCodecID codec;
} media_format_t;

static const media_format_t media_formats[] = {
	{ ".ogv", "ogg", AV_CODEC_ID_THEORA },
	{ ".mkv", "matroska", AV_CODEC_ID_NONE },
	{ ".mp4", "mov", AV_CODEC_ID_H264 },
	{ ".cin", "idcin", AV_CODEC_ID_IDCIN }
};

typedef struct media_packet_s
{
	AVPacket *packet;
	size_t bytes;
	struct media_packet_s *next;
} media_packet_t;

typedef struct
{
	AVCodecContext *context;
	AVFrame *frame;
	media_packet_t *head, *tail;
	int index, flush_sent, eof;
} media_decoder_t;

struct cl_media_s
{
	FILE *file;
	int64_t length, position;
	AVFormatContext *format;
	AVIOContext *io;
	AVPacket *packet;
	media_decoder_t decoder[2]; /* video, audio */
	struct SwsContext *sws;
	struct SwrContext *swr;
	AVChannelLayout audio_layout;
	unsigned char *rgba;
	size_t rgba_size;
	int audio_rate, input_rate, input_format, audio_offset;
	int demux_eof, packet_pending, next_decoder, failed, idcin;
	size_t queue_bytes;
	int queue_packets;
	int audio_clock_set, audio_finished;
	double origin, video_end, audio_end, audio_input_end;
	int16_t pcm[MEDIA_AUDIO_FRAMES * 2];
	char failure[160];
};

static const media_format_t *CL_MediaFormat (const char *extension)
{
	size_t i, j;
	if (!extension)
		return NULL;
	for (i = 0; i < sizeof (media_formats) / sizeof (media_formats[0]); i++)
	{
		const char *expected = media_formats[i].extension;
		for (j = 0; expected[j]; j++)
		{
			int c = (unsigned char)extension[j];
			if (c >= 'A' && c <= 'Z')
				c += 'a' - 'A';
			if (c != expected[j])
				break;
		}
		if (!expected[j] && !extension[j])
			return &media_formats[i];
	}
	return NULL;
}

int CL_MediaFormatAvailable (const char *extension)
{
	const media_format_t *format = CL_MediaFormat (extension);
	return format && av_find_input_format (format->demuxer) &&
		(format->codec == AV_CODEC_ID_NONE || avcodec_find_decoder (format->codec));
}

static int CL_MediaFileRead (void *opaque, uint8_t *buffer, int size)
{
	cl_media_t *media = opaque;
	int64_t remaining = media->length - media->position;
	size_t count;
	if (size < 0)
		return AVERROR (EINVAL);
	if (size > remaining)
		size = (int)remaining;
	if (!size)
		return AVERROR_EOF;
	count = fread (buffer, 1, size, media->file);
	if (!count)
		return ferror (media->file) ? AVERROR (EIO) : AVERROR_EOF;
	media->position += count;
	return (int)count;
}

static int64_t CL_MediaFileSeek (void *opaque, int64_t offset, int whence)
{
	cl_media_t *media = opaque;
	int64_t base;
	whence &= ~AVSEEK_FORCE;
	if (whence == AVSEEK_SIZE)
		return media->length;
	switch (whence)
	{
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = media->position;
		break;
	case SEEK_END:
		base = media->length;
		break;
	default:
		return AVERROR (EINVAL);
	}
	if (offset < -base || offset > media->length - base)
		return AVERROR (EINVAL);
	if (CL_MediaSeek (media->file, base + offset, SEEK_SET))
		return AVERROR (EIO);
	media->position = base + offset;
	return media->position;
}

/* Containers may ask for subordinate streams even with custom primary IO. */
static int CL_MediaRejectIO (AVFormatContext *context, AVIOContext **io,
	const char *url, int flags, AVDictionary **options)
{
	(void)context;
	(void)io;
	(void)url;
	(void)flags;
	(void)options;
	return AVERROR (EACCES);
}

void CL_MediaClose (cl_media_t **owner)
{
	cl_media_t *media;
	int i;
	if (!owner || !*owner)
		return;
	media = *owner;
	*owner = NULL;
	avformat_close_input (&media->format);
	if (media->io)
		av_freep (&media->io->buffer);
	avio_context_free (&media->io);
	for (i = 0; i < 2; i++)
	{
		media_packet_t *packet = media->decoder[i].head;
		while (packet)
		{
			media_packet_t *next = packet->next;
			av_packet_free (&packet->packet);
			av_free (packet);
			packet = next;
		}
		avcodec_free_context (&media->decoder[i].context);
		av_frame_free (&media->decoder[i].frame);
	}
	av_packet_free (&media->packet);
	sws_freeContext (media->sws);
	swr_free (&media->swr);
	av_channel_layout_uninit (&media->audio_layout);
	av_free (media->rgba);
	av_free (media);
}

static int CL_MediaOpenDecoder (cl_media_t *media, int kind)
{
	media_decoder_t *decoder = &media->decoder[kind];
	const AVCodec *codec;
	AVStream *stream;
	int ret;
	ret = av_find_best_stream (media->format, kind ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO,
		-1, -1, NULL, 0);
	if (ret == AVERROR_STREAM_NOT_FOUND && kind)
	{
		decoder->index = -1;
		decoder->eof = media->audio_finished = 1;
		return 0;
	}
	if (ret < 0)
		return ret;
	decoder->index = ret;
	stream = media->format->streams[ret];
	codec = avcodec_find_decoder (stream->codecpar->codec_id);
	if (!codec)
		return AVERROR_DECODER_NOT_FOUND;
	decoder->context = avcodec_alloc_context3 (codec);
	decoder->frame = av_frame_alloc ();
	if (!decoder->context || !decoder->frame)
		return AVERROR (ENOMEM);
	ret = avcodec_parameters_to_context (decoder->context, stream->codecpar);
	if (ret < 0)
		return ret;
	decoder->context->pkt_timebase = stream->time_base;
	decoder->context->thread_count = 1;
	decoder->context->max_pixels = (int64_t)MEDIA_MAX_AXIS * MEDIA_MAX_AXIS;
	decoder->context->max_samples = MEDIA_MAX_SAMPLES;
	return avcodec_open2 (decoder->context, codec, NULL);
}

cl_media_t *CL_MediaOpen (FILE *file, const char *extension, int audio_rate,
	char *error, int error_size)
{
	const media_format_t *selected = CL_MediaFormat (extension);
	const AVInputFormat *demuxer;
	cl_media_t *media = NULL;
	unsigned char *buffer = NULL;
	int64_t length;
	int ret;
	CL_MediaMessage (error, error_size, "");
	if (!file || !selected || audio_rate < 1 || audio_rate > 192000)
	{
		CL_MediaMessage (error, error_size, "Invalid movie file, extension or audio rate");
		return NULL;
	}
	demuxer = av_find_input_format (selected->demuxer);
	if (!demuxer)
	{
		CL_MediaMessage (error, error_size, "Movie demuxer unavailable in this build");
		return NULL;
	}
	if (CL_MediaSeek (file, 0, SEEK_END) || (length = CL_MediaTell (file)) <= 0 ||
		CL_MediaSeek (file, 0, SEEK_SET))
	{
		CL_MediaMessage (error, error_size, "Movie requires a nonempty seekable file");
		return NULL;
	}
	media = av_mallocz (sizeof (*media));
	if (!media)
	{
		ret = AVERROR (ENOMEM);
		goto fail;
	}
	media->file = file;
	media->length = length;
	media->audio_rate = audio_rate;
	media->idcin = !strcmp (selected->demuxer, "idcin");
	media->decoder[0].index = media->decoder[1].index = -1;
	media->format = avformat_alloc_context ();
	media->packet = av_packet_alloc ();
	buffer = av_malloc (4096);
	if (!media->format || !media->packet || !buffer)
	{
		ret = AVERROR (ENOMEM);
		goto fail;
	}
	media->io = avio_alloc_context (buffer, 4096, 0, media, CL_MediaFileRead, NULL, CL_MediaFileSeek);
	if (!media->io)
	{
		ret = AVERROR (ENOMEM);
		goto fail;
	}
	buffer = NULL;
	media->format->pb = media->io;
	media->format->flags |= AVFMT_FLAG_CUSTOM_IO;
	media->format->io_open = CL_MediaRejectIO;
	/* Only the borrowed FILE is readable; never pass a filename or URL. */
	ret = avformat_open_input (&media->format, NULL, demuxer, NULL);
	if (ret < 0)
		goto fail;
	ret = avformat_find_stream_info (media->format, NULL);
	if (ret < 0)
		goto fail;
	ret = CL_MediaOpenDecoder (media, 0);
	if (ret < 0)
		goto fail;
	ret = CL_MediaOpenDecoder (media, 1);
	if (ret < 0)
		goto fail;
	if (media->format->start_time != AV_NOPTS_VALUE)
		media->origin = (double)media->format->start_time / AV_TIME_BASE;
	return media;
fail:
	if (error && error_size > 0)
		av_strerror (ret, error, error_size);
	av_free (buffer);
	CL_MediaClose (&media);
	return NULL;
}

static double CL_MediaTime (cl_media_t *media, int kind, double fallback)
{
	media_decoder_t *decoder = &media->decoder[kind];
	AVRational base = decoder->context->pkt_timebase;
	int64_t pts = decoder->frame->best_effort_timestamp;
	double value;
	if (pts == AV_NOPTS_VALUE || base.num <= 0 || base.den <= 0)
		return fallback;
	value = pts * av_q2d (base) - media->origin;
	if (!(value >= -1.0e12 && value <= 1.0e12))
		return fallback;
	return value < 0 ? 0 : value;
}

static int CL_MediaVideo (cl_media_t *media, cl_media_frame_t *out)
{
	AVFrame *frame = media->decoder[0].frame;
	uint8_t *planes[4] = { NULL, NULL, NULL, NULL };
	int strides[4] = { 0, 0, 0, 0 };
	int stride, ret, colorspace;
	size_t size;
	AVRational rate;
	int64_t duration;
	if (frame->width < 1 || frame->height < 1 || frame->width > MEDIA_MAX_AXIS ||
		frame->height > MEDIA_MAX_AXIS || !frame->data[0] || !sws_isSupportedInput (frame->format))
		return AVERROR_INVALIDDATA;
	stride = (frame->width * 4 + 31) & ~31;
	size = (size_t)stride * frame->height;
	if (size != media->rgba_size)
	{
		unsigned char *pixels = av_realloc (media->rgba, size + AV_INPUT_BUFFER_PADDING_SIZE);
		if (!pixels)
			return AVERROR (ENOMEM);
		media->rgba = pixels;
		media->rgba_size = size;
	}
	media->sws = sws_getCachedContext (media->sws, frame->width, frame->height, frame->format,
		frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
	if (!media->sws)
		return AVERROR (ENOMEM);
	switch (frame->colorspace)
	{
	case AVCOL_SPC_BT709:
		colorspace = SWS_CS_ITU709;
		break;
	case AVCOL_SPC_FCC:
		colorspace = SWS_CS_FCC;
		break;
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
		colorspace = SWS_CS_ITU601;
		break;
	case AVCOL_SPC_SMPTE240M:
		colorspace = SWS_CS_SMPTE240M;
		break;
	case AVCOL_SPC_BT2020_NCL:
		colorspace = SWS_CS_BT2020;
		break;
	default:
		colorspace = SWS_CS_DEFAULT;
		break;
	}
	ret = sws_setColorspaceDetails (media->sws, sws_getCoefficients (colorspace),
		frame->color_range == AVCOL_RANGE_JPEG, sws_getCoefficients (colorspace), 1, 0, 1 << 16, 1 << 16);
	if (ret < 0)
		return ret;
	memset (media->rgba, 0, size + AV_INPUT_BUFFER_PADDING_SIZE);
	planes[0] = media->rgba;
	strides[0] = stride;
	ret = sws_scale (media->sws, (const uint8_t *const *)frame->data, frame->linesize,
		0, frame->height, planes, strides);
	if (ret != frame->height)
		return AVERROR_INVALIDDATA;
	out->type = CL_MEDIA_VIDEO;
	out->data = media->rgba;
	out->size = size;
	out->width = frame->width;
	out->height = frame->height;
	out->stride = stride;
	out->time = CL_MediaTime (media, 0, media->video_end);
#if LIBAVUTIL_VERSION_MAJOR >= 58
	duration = frame->duration;
#else
	duration = frame->pkt_duration;
#endif
	out->duration = duration > 0 ? duration * av_q2d (media->decoder[0].context->pkt_timebase) : 0;
	if (!(out->duration > 0 && out->duration <= 3600))
	{
		rate = av_guess_frame_rate (media->format, media->format->streams[media->decoder[0].index], frame);
		out->duration = rate.num > 0 && rate.den > 0 ? (double)rate.den / rate.num : 1.0 / 25;
		if (!(out->duration > 0 && out->duration <= 3600))
			out->duration = 1.0 / 25;
	}
	media->video_end = out->time + out->duration;
	av_frame_unref (frame);
	return 1;
}

static int CL_MediaAudioValid (AVFrame *frame)
{
	int channels = frame->ch_layout.nb_channels;
	if (channels < 1 || channels > MEDIA_MAX_CHANNELS || frame->sample_rate <= 0 ||
		frame->nb_samples < 0 || frame->nb_samples > MEDIA_MAX_SAMPLES / channels)
		return 0;
	if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
	{
		av_channel_layout_uninit (&frame->ch_layout);
		av_channel_layout_default (&frame->ch_layout, channels);
	}
	return av_channel_layout_check (&frame->ch_layout) && av_get_bytes_per_sample (frame->format) > 0;
}

static int CL_MediaAudioConvert (cl_media_t *media, const uint8_t **input, int samples,
	cl_media_frame_t *out)
{
	unsigned char *data = (unsigned char *)media->pcm;
	const uint16_t little = 1;
	int count, i;
	count = swr_convert (media->swr, &data, MEDIA_AUDIO_FRAMES, input, samples);
	if (count <= 0)
		return count;
	if (!*(const unsigned char *)&little)
		for (i = 0; i < count * 4; i += 2)
		{
			unsigned char byte = data[i];
			data[i] = data[i + 1];
			data[i + 1] = byte;
		}
	out->type = CL_MEDIA_AUDIO;
	out->data = data;
	out->size = count * 4;
	out->samples = count;
	out->rate = media->audio_rate;
	out->time = media->audio_end;
	out->duration = (double)count / media->audio_rate;
	media->audio_end += out->duration;
	return 1;
}

static int CL_MediaAudio (cl_media_t *media, cl_media_frame_t *out)
{
	AVFrame *frame = media->decoder[1].frame;
	AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	const uint8_t *planes[MEDIA_MAX_CHANNELS] = { NULL };
	int ret, changed, channels, bytes, count, i, planar;
	double time;
	if (media->audio_finished)
		return 0;
	if (frame->nb_samples)
	{
		if (!CL_MediaAudioValid (frame))
			return AVERROR_INVALIDDATA;
		time = CL_MediaTime (media, 1, media->audio_input_end);
		changed = media->swr && !media->audio_offset &&
			(media->input_rate != frame->sample_rate || media->input_format != frame->format ||
				av_channel_layout_compare (&media->audio_layout, &frame->ch_layout) ||
				time > media->audio_input_end + 0.1 || time < media->audio_input_end - 0.1);
		if (changed)
		{
			ret = CL_MediaAudioConvert (media, NULL, 0, out);
			if (ret)
				return ret;
			swr_free (&media->swr);
			media->audio_clock_set = 0;
		}
		if (!media->swr)
		{
			av_channel_layout_uninit (&media->audio_layout);
			ret = av_channel_layout_copy (&media->audio_layout, &frame->ch_layout);
			if (ret < 0)
				return ret;
			media->input_rate = frame->sample_rate;
			media->input_format = frame->format;
			ret = swr_alloc_set_opts2 (&media->swr, &stereo, AV_SAMPLE_FMT_S16, media->audio_rate,
				&media->audio_layout, frame->format, frame->sample_rate, 0, NULL);
			if (ret < 0)
				return ret;
			ret = swr_init (media->swr);
			if (ret < 0)
				return ret;
		}
		if (!media->audio_clock_set)
		{
			media->audio_end = time;
			media->audio_clock_set = 1;
		}
		/* Empty non-NULL input drains queued output without ending the filter. */
		ret = CL_MediaAudioConvert (media, planes, 0, out);
		if (ret)
			return ret;
		channels = frame->ch_layout.nb_channels;
		bytes = av_get_bytes_per_sample (frame->format);
		planar = av_sample_fmt_is_planar (frame->format);
		count = (int)((int64_t)MEDIA_AUDIO_FRAMES * frame->sample_rate / media->audio_rate);
		if (count < 1)
			count = 1;
		if (count > 1024)
			count = 1024;
		if (count > frame->nb_samples - media->audio_offset)
			count = frame->nb_samples - media->audio_offset;
		for (i = 0; i < (planar ? channels : 1); i++)
		{
			if (!frame->extended_data[i])
				return AVERROR_INVALIDDATA;
			planes[i] = frame->extended_data[i] + (size_t)media->audio_offset * bytes * (planar ? 1 : channels);
		}
		ret = CL_MediaAudioConvert (media, planes, count, out);
		media->audio_offset += count;
		if (media->audio_offset == frame->nb_samples)
		{
			media->audio_input_end = time + (double)frame->nb_samples / frame->sample_rate;
			media->audio_offset = 0;
			av_frame_unref (frame);
		}
		return ret;
	}
	if (media->swr)
	{
		ret = CL_MediaAudioConvert (media, media->decoder[1].eof ? NULL : planes, 0, out);
		if (ret)
			return ret;
	}
	if (media->decoder[1].eof)
		media->audio_finished = 1;
	return 0;
}

static int CL_MediaFailure (cl_media_t *media, int code, char *error, int size)
{
	media->failed = 1;
	av_strerror (code, media->failure, sizeof (media->failure));
	CL_MediaMessage (error, size, media->failure);
	return CL_MEDIA_ERROR;
}

/* The one pending demux packet is retained when the queues cannot admit it.
 * Its own stream can still consume it directly after its older packets. */
static int CL_MediaQueuePacket (cl_media_t *media, int kind)
{
	media_decoder_t *decoder = &media->decoder[kind];
	media_packet_t *queued;
	size_t bytes = (size_t)media->packet->size;
	int i;
	for (i = 0; i < media->packet->side_data_elems; i++)
	{
		if (media->packet->side_data[i].size > (size_t)MEDIA_QUEUE_BYTES ||
			bytes > (size_t)MEDIA_QUEUE_BYTES - media->packet->side_data[i].size)
			return 0;
		bytes += media->packet->side_data[i].size;
	}
	if (bytes > (size_t)MEDIA_QUEUE_BYTES ||
		media->queue_bytes > (size_t)MEDIA_QUEUE_BYTES - bytes ||
		media->queue_packets >= MEDIA_QUEUE_PACKETS)
		return 0;
	queued = av_mallocz (sizeof (*queued));
	if (!queued)
		return AVERROR (ENOMEM);
	queued->packet = av_packet_alloc ();
	if (!queued->packet)
	{
		av_free (queued);
		return AVERROR (ENOMEM);
	}
	av_packet_move_ref (queued->packet, media->packet);
	queued->bytes = bytes;
	if (decoder->tail)
		decoder->tail->next = queued;
	else
		decoder->head = queued;
	decoder->tail = queued;
	media->queue_bytes += bytes;
	media->queue_packets++;
	media->packet_pending = 0;
	return 1;
}

static void CL_MediaPopPacket (cl_media_t *media, int kind)
{
	media_decoder_t *decoder = &media->decoder[kind];
	media_packet_t *queued = decoder->head;
	decoder->head = queued->next;
	if (!decoder->head)
		decoder->tail = NULL;
	media->queue_bytes -= queued->bytes;
	media->queue_packets--;
	av_packet_free (&queued->packet);
	av_free (queued);
}

static int CL_MediaRead (cl_media_t *media, int wanted, cl_media_frame_t *out,
	char *error, int error_size)
{
	int step, i, kind, ret, fed;
	media_decoder_t *decoder;
	CL_MediaMessage (error, error_size, "");
	if (!media || !out)
	{
		CL_MediaMessage (error, error_size, "Invalid movie read arguments");
		return CL_MEDIA_ERROR;
	}
	memset (out, 0, sizeof (*out));
	if (media->failed)
	{
		CL_MediaMessage (error, error_size, media->failure);
		return CL_MEDIA_ERROR;
	}
	for (step = 0; step < MEDIA_STEPS; step++)
	{
		if (wanted != 0)
		{
			ret = CL_MediaAudio (media, out);
			if (ret < 0)
				return CL_MediaFailure (media, ret, error, error_size);
			if (ret)
				return CL_MEDIA_EVENT;
			if (media->decoder[1].frame && media->decoder[1].frame->nb_samples)
				continue;
		}
		for (i = 0; i < 2; i++)
		{
			kind = (media->next_decoder + i) & 1;
			if (wanted >= 0 && wanted != kind)
				continue;
			decoder = &media->decoder[kind];
			if (decoder->eof)
				continue;
			ret = avcodec_receive_frame (decoder->context, decoder->frame);
			if (ret == AVERROR_EOF)
			{
				if (!decoder->flush_sent)
					return CL_MediaFailure (media, AVERROR_INVALIDDATA, error, error_size);
				decoder->eof = 1;
				continue;
			}
			if (ret == AVERROR (EAGAIN))
				continue;
			if (ret < 0)
				return CL_MediaFailure (media, ret, error, error_size);
			media->next_decoder = kind ^ 1;
			if (kind)
				break; /* convert the retained audio frame on the next step */
			ret = CL_MediaVideo (media, out);
			if (ret < 0)
				return CL_MediaFailure (media, ret, error, error_size);
			return CL_MEDIA_EVENT;
		}
		if (wanted != 0 && media->decoder[1].frame && media->decoder[1].frame->nb_samples)
			continue;
		if ((wanted == 0 && media->decoder[0].eof) ||
			(wanted == 1 && media->audio_finished) ||
			(wanted < 0 && media->decoder[0].eof && media->audio_finished))
			return CL_MEDIA_EOF;
		/* Consume queued input before newer pending input or any drain packet. */
		fed = 0;
		for (i = 0; i < 2; i++)
		{
			kind = (media->next_decoder + i) & 1;
			if (wanted >= 0 && wanted != kind)
				continue;
			decoder = &media->decoder[kind];
			if (!decoder->head)
				continue;
			ret = avcodec_send_packet (decoder->context, decoder->head->packet);
			if (ret == AVERROR (EAGAIN))
			{
				fed = 1;
				continue;
			}
			if (ret < 0)
				return CL_MediaFailure (media, ret, error, error_size);
			CL_MediaPopPacket (media, kind);
			fed = 1;
		}
		if (fed)
			continue;
		if (media->packet_pending)
		{
			kind = media->packet->stream_index == media->decoder[0].index ? 0 :
																			(media->packet->stream_index == media->decoder[1].index ? 1 : -1);
			if (kind >= 0)
			{
				if (wanted < 0 || wanted == kind)
				{
					ret = avcodec_send_packet (media->decoder[kind].context, media->packet);
					if (ret == AVERROR (EAGAIN))
						continue;
					if (ret < 0)
						return CL_MediaFailure (media, ret, error, error_size);
				}
				else
				{
					ret = CL_MediaQueuePacket (media, kind);
					if (ret < 0)
						return CL_MediaFailure (media, ret, error, error_size);
					if (!ret)
						return CL_MEDIA_AGAIN;
					continue;
				}
			}
			av_packet_unref (media->packet);
			media->packet_pending = 0;
			continue;
		}
		if (media->demux_eof)
		{
			for (i = 0; i < 2; i++)
			{
				if (wanted >= 0 && wanted != i)
					continue;
				decoder = &media->decoder[i];
				if (decoder->eof || decoder->flush_sent)
					continue;
				ret = avcodec_send_packet (decoder->context, NULL);
				if (ret == AVERROR (EAGAIN))
					continue;
				if (ret < 0)
					return CL_MediaFailure (media, ret, error, error_size);
				decoder->flush_sent = 1;
			}
			continue;
		}
		ret = av_read_frame (media->format, media->packet);
		if (ret == AVERROR_EOF || (media->idcin && ret == AVERROR (EIO)))
		{
			media->demux_eof = 1;
			continue;
		}
		if (ret < 0)
			return CL_MediaFailure (media, ret, error, error_size);
		if (media->packet->size < 0 || media->packet->size > MEDIA_PACKET_BYTES)
			return CL_MediaFailure (media, AVERROR_INVALIDDATA, error, error_size);
		media->packet_pending = 1;
	}
	return CL_MEDIA_AGAIN;
}

int CL_MediaNext (cl_media_t *media, cl_media_frame_t *out, char *error, int error_size)
{
	return CL_MediaRead (media, -1, out, error, error_size);
}

int CL_MediaNextStream (cl_media_t *media, int type, cl_media_frame_t *out,
	char *error, int error_size)
{
	if (type != CL_MEDIA_VIDEO && type != CL_MEDIA_AUDIO)
	{
		CL_MediaMessage (error, error_size, "Invalid movie stream type");
		return CL_MEDIA_ERROR;
	}
	return CL_MediaRead (media, type == CL_MEDIA_AUDIO, out, error, error_size);
}

#else

int CL_MediaFormatAvailable (const char *extension)
{
	(void)extension;
	return 0;
}

cl_media_t *CL_MediaOpen (FILE *file, const char *extension, int rate, char *error, int size)
{
	(void)file;
	(void)extension;
	(void)rate;
	CL_MediaMessage (error, size, "Movie codecs unavailable in this build");
	return NULL;
}

int CL_MediaNext (cl_media_t *media, cl_media_frame_t *frame, char *error, int size)
{
	(void)media;
	(void)frame;
	CL_MediaMessage (error, size, "Movie codecs unavailable in this build");
	return CL_MEDIA_ERROR;
}

int CL_MediaNextStream (cl_media_t *media, int type, cl_media_frame_t *frame,
	char *error, int size)
{
	(void)type;
	return CL_MediaNext (media, frame, error, size);
}

void CL_MediaClose (cl_media_t **media)
{
	if (media)
		*media = NULL;
}
#endif
