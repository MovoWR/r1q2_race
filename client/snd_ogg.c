/*
 * Vorbis sound-asset loading adapted from Q2PRO sound/mem.c,
 * revision 601a8df8433b0c50dbbe37c0716c3793fff140a7.
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later version.
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See COPYING.
 */

#include "snd_ogg.h"
#include <string.h>

static void S_OggError (char *error, int size, const char *message)
{
	if (error && size > 0)
	{
		strncpy (error, message, size - 1);
		error[size - 1] = 0;
	}
}

int S_IsOggSound (const char *name, const unsigned char *data, int length)
{
	size_t size;
	if (data && length >= 4 && !memcmp (data, "OggS", 4))
		return 1;
	if (!name)
		return 0;
	size = strlen (name);
	if (size < 4)
		return 0;
	name += size - 4;
	return name[0] == '.' && (name[1] == 'o' || name[1] == 'O') &&
		(name[2] == 'g' || name[2] == 'G') && (name[3] == 'g' || name[3] == 'G');
}

#ifdef USE_AVCODEC
#include <stdint.h>
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 37, 100) ||  \
	LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 27, 100) || \
	LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 28, 100) ||   \
	LIBSWRESAMPLE_VERSION_INT < AV_VERSION_INT(4, 7, 100)
#error Unsupported FFmpeg version for Vorbis sound effects
#endif

typedef struct
{
	const unsigned char *data;
	int length, offset;
} ogg_input_t;

static int S_OggRead (void *opaque, uint8_t *buffer, int size)
{
	ogg_input_t *input = opaque;
	int remaining = input->length - input->offset;
	if (size < 0)
		return AVERROR (EINVAL);
	if (size > remaining)
		size = remaining;
	if (!size)
		return AVERROR_EOF;
	memcpy (buffer, input->data + input->offset, size);
	input->offset += size;
	return size;
}

static int64_t S_OggSeek (void *opaque, int64_t offset, int whence)
{
	ogg_input_t *input = opaque;
	int base;

	whence &= ~AVSEEK_FORCE;
	if (whence == AVSEEK_SIZE)
		return input->length;
	switch (whence)
	{
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = input->offset;
		break;
	case SEEK_END:
		base = input->length;
		break;
	default:
		return AVERROR (EINVAL);
	}
	if (offset < -(int64_t)base || offset > (int64_t)input->length - base)
		return AVERROR (EINVAL);
	input->offset = base + (int)offset;
	return input->offset;
}

static int S_OggAppend (snd_ogg_t *pcm, int *capacity,
	const unsigned char *data, int frames)
{
	unsigned char *grown, *destination;
	int needed, next, i, samples;
	const uint16_t little = 1;

	if (frames < 0 || frames > S_OGG_MAX_FRAMES - pcm->frames)
		return AVERROR (EFBIG);
	if (!frames)
		return 0;
	needed = pcm->frames + frames;
	if (needed > *capacity)
	{
		next = *capacity ? *capacity : 4096;
		while (next < needed)
			next = next > S_OGG_MAX_FRAMES / 2 ? S_OGG_MAX_FRAMES : next * 2;
		grown = av_realloc (pcm->data, (size_t)next * pcm->channels * 2);
		if (!grown)
			return AVERROR (ENOMEM);
		pcm->data = grown;
		*capacity = next;
	}
	destination = pcm->data + (size_t)pcm->frames * pcm->channels * 2;
	samples = frames * pcm->channels;
	if (*(const unsigned char *)&little)
		memcpy (destination, data, (size_t)samples * 2);
	else
	{
		for (i = 0; i < samples; i++)
		{
			destination[i * 2] = data[i * 2 + 1];
			destination[i * 2 + 1] = data[i * 2];
		}
	}
	pcm->frames = needed;
	return 0;
}

int S_DecodeOgg (const unsigned char *data, int length, snd_ogg_t *out,
	char *error, int error_size)
{
	AVFormatContext *format = NULL;
	AVIOContext *io = NULL;
	AVCodecContext *decoder = NULL;
	AVPacket *packet = NULL;
	AVFrame *frame = NULL;
	AVChannelLayout layout = { 0 };
	struct SwrContext *resampler = NULL;
	const AVInputFormat *input_format;
	const AVCodec *codec;
	AVStream *stream;
	ogg_input_t input;
	snd_ogg_t pcm = { 0 };
	unsigned char *io_buffer = NULL, *converted = NULL, *grown;
	int ret = 0, success = 0, capacity = 0, converted_frames = 0;
	int received = 0, draining = 0, sample_format = -1;
	const char *failure = NULL;

	S_OggError (error, error_size, "");
	if (!out)
	{
		S_OggError (error, error_size, "Missing Vorbis output");
		return 0;
	}
	memset (out, 0, sizeof (*out));
	if (!data || length <= 0)
	{
		S_OggError (error, error_size, "Empty Vorbis input");
		return 0;
	}
	input.data = data;
	input.length = length;
	input.offset = 0;
	input_format = av_find_input_format ("ogg");
	codec = avcodec_find_decoder (AV_CODEC_ID_VORBIS);
	if (!input_format || !codec)
	{
		failure = "Ogg demuxer or Vorbis decoder unavailable";
		goto done;
	}
	format = avformat_alloc_context ();
	io_buffer = av_malloc (4096);
	if (!format || !io_buffer)
	{
		ret = AVERROR (ENOMEM);
		goto done;
	}
	io = avio_alloc_context (io_buffer, 4096, 0, &input, S_OggRead, NULL, S_OggSeek);
	if (!io)
	{
		ret = AVERROR (ENOMEM);
		goto done;
	}
	io_buffer = NULL;
	format->pb = io;
	format->flags |= AVFMT_FLAG_CUSTOM_IO;
	/* A forced demuxer and custom memory IO cannot open local files or URLs. */
	ret = avformat_open_input (&format, NULL, input_format, NULL);
	if (ret < 0)
		goto done;
	if (format->nb_streams != 1)
	{
		failure = "Vorbis effects require one stream";
		goto done;
	}
	stream = format->streams[0];
	if (stream->codecpar->codec_id != AV_CODEC_ID_VORBIS ||
		stream->codecpar->ch_layout.nb_channels < 1 || stream->codecpar->ch_layout.nb_channels > 2 ||
		stream->codecpar->sample_rate < 6000 || stream->codecpar->sample_rate > 48000)
	{
		failure = "Vorbis effects require mono/stereo at 6000-48000 Hz";
		goto done;
	}
	pcm.rate = stream->codecpar->sample_rate;
	pcm.channels = stream->codecpar->ch_layout.nb_channels;
	decoder = avcodec_alloc_context3 (codec);
	packet = av_packet_alloc ();
	frame = av_frame_alloc ();
	if (!decoder || !packet || !frame)
	{
		ret = AVERROR (ENOMEM);
		goto done;
	}
	ret = avcodec_parameters_to_context (decoder, stream->codecpar);
	if (ret < 0)
		goto done;
	decoder->pkt_timebase = stream->time_base;
	decoder->max_samples = S_OGG_MAX_FRAMES;
	decoder->thread_count = 1;
	ret = avcodec_open2 (decoder, codec, NULL);
	if (ret < 0)
		goto done;

	for (;;)
	{
		ret = avcodec_receive_frame (decoder, frame);
		if (ret == AVERROR_EOF)
		{
			if (!draining)
			{
				failure = "Vorbis decoder ended before input drain";
				goto done;
			}
			break;
		}
		if (ret == AVERROR (EAGAIN))
		{
			if (draining)
			{
				failure = "Vorbis decoder did not finish draining";
				goto done;
			}
			ret = av_read_frame (format, packet);
			if (ret == AVERROR_EOF)
			{
				ret = avcodec_send_packet (decoder, NULL);
				draining = 1;
			}
			else if (ret >= 0)
			{
				if (format->nb_streams != 1 || packet->stream_index != 0)
				{
					failure = "Multiple Vorbis streams are unsupported";
					goto done;
				}
				ret = avcodec_send_packet (decoder, packet);
				/* A send after receive EAGAIN must accept input. On failure
				 * retain packet ownership until common cleanup; never drop it
				 * and continue decoding a partial sound. */
				if (ret >= 0)
					av_packet_unref (packet);
			}
			if (ret < 0)
				goto done;
			continue;
		}
		if (ret < 0)
			goto done;
		if (frame->nb_samples < 0 || frame->nb_samples > S_OGG_MAX_FRAMES - received ||
			frame->sample_rate != pcm.rate || frame->ch_layout.nb_channels != pcm.channels)
		{
			failure = "Vorbis decoded length or format exceeds sound limits";
			goto done;
		}
		received += frame->nb_samples;
		if (!resampler)
		{
			ret = av_channel_layout_copy (&layout, &frame->ch_layout);
			if (ret < 0)
				goto done;
			sample_format = frame->format;
			ret = swr_alloc_set_opts2 (&resampler, &layout, AV_SAMPLE_FMT_S16, pcm.rate,
				&layout, sample_format, pcm.rate, 0, NULL);
			if (ret < 0)
				goto done;
			ret = swr_init (resampler);
			if (ret < 0)
				goto done;
		}
		else if (frame->format != sample_format || av_channel_layout_compare (&layout, &frame->ch_layout))
		{
			failure = "Vorbis sound format changed while decoding";
			goto done;
		}
		if (frame->nb_samples > converted_frames || !converted)
		{
			int count = frame->nb_samples > 4096 ? frame->nb_samples : 4096;
			grown = av_realloc (converted, (size_t)count * pcm.channels * 2);
			if (!grown)
			{
				ret = AVERROR (ENOMEM);
				goto done;
			}
			converted = grown;
			converted_frames = count;
		}
		ret = swr_convert (resampler, &converted, converted_frames,
			(const uint8_t **)frame->extended_data, frame->nb_samples);
		if (ret < 0)
			goto done;
		ret = S_OggAppend (&pcm, &capacity, converted, ret);
		if (ret < 0)
			goto done;
		av_frame_unref (frame);
	}
	if (!resampler || !received)
	{
		failure = "Vorbis sound has no decoded samples";
		goto done;
	}
	for (;;)
	{
		ret = swr_convert (resampler, &converted, converted_frames, NULL, 0);
		if (ret < 0)
			goto done;
		if (!ret)
			break;
		ret = S_OggAppend (&pcm, &capacity, converted, ret);
		if (ret < 0)
			goto done;
	}
	if (!pcm.frames)
	{
		failure = "Vorbis sound has no converted samples";
		goto done;
	}
	*out = pcm;
	memset (&pcm, 0, sizeof (pcm));
	success = 1;

done:
	if (!success)
	{
		if (failure)
			S_OggError (error, error_size, failure);
		else if (error && error_size > 0)
			av_strerror (ret, error, error_size);
	}
	avformat_close_input (&format);
	if (io)
		av_freep (&io->buffer);
	avio_context_free (&io);
	av_free (io_buffer);
	avcodec_free_context (&decoder);
	av_packet_free (&packet);
	av_frame_free (&frame);
	av_channel_layout_uninit (&layout);
	swr_free (&resampler);
	av_free (converted);
	av_free (pcm.data);
	return success;
}

#else

int S_DecodeOgg (const unsigned char *data, int length, snd_ogg_t *out,
	char *error, int error_size)
{
	(void)data;
	(void)length;
	if (out)
		memset (out, 0, sizeof (*out));
	S_OggError (error, error_size, "Vorbis sound effects are unavailable in this build");
	return 0;
}

#endif

void S_FreeOgg (snd_ogg_t *out)
{
	if (!out)
		return;
#ifdef USE_AVCODEC
	av_free (out->data);
#endif
	memset (out, 0, sizeof (*out));
}
