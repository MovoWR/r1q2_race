/*
 * Local music decoding adapted from Q2PRO src/client/sound/ogg.c,
 * revision 601a8df8433b0c50dbbe37c0716c3793fff140a7.
 * Copyright (C) 2022 Andrey Nazarov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the GNU General Public License, version 2 or (at your option) later.
 * This program is distributed WITHOUT ANY WARRANTY. See COPYING.
 */
#if !defined(_WIN32) && defined(USE_AVCODEC)
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif

#include "snd_stream.h"
#include <string.h>

static void S_StreamMessage (char *error, int size, const char *message)
{
	if (error && size > 0)
	{
		strncpy (error, message, size - 1);
		error[size - 1] = 0;
	}
}

#ifdef USE_AVCODEC
#include <stdint.h>
#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 37, 100) ||  \
	LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 27, 100) || \
	LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 28, 100) ||   \
	LIBSWRESAMPLE_VERSION_INT < AV_VERSION_INT(4, 7, 100)
#error Unsupported FFmpeg version for file music
#endif

#ifdef _WIN32
#define S_StreamFileSeek _fseeki64
#define S_StreamFileTell _ftelli64
#else
#define S_StreamFileSeek fseeko
#define S_StreamFileTell ftello
#endif

#define STREAM_BLOCK_FRAMES 4096
#define STREAM_MAX_CHANNELS 64
#define STREAM_MAX_SAMPLES 1048576

typedef struct
{
	const char *extension, *demuxer;
	enum AVCodecID codec;
} stream_format_t;

static const stream_format_t stream_formats[] = {
	{ ".flac", "flac", AV_CODEC_ID_FLAC },
	{ ".opus", "ogg", AV_CODEC_ID_OPUS },
	{ ".ogg", "ogg", AV_CODEC_ID_VORBIS },
	{ ".mp3", "mp3", AV_CODEC_ID_MP3 },
	{ ".wav", "wav", AV_CODEC_ID_NONE }
};

struct snd_stream_s
{
	FILE *file;
	int64_t length, position;
	AVFormatContext *format;
	AVIOContext *io;
	AVCodecContext *decoder;
	AVPacket *packet;
	AVFrame *frame;
	struct SwrContext *swr;
	AVChannelLayout layout;
	int input_rate, input_format, output_rate, stream_index;
	int draining, decoder_eof, frame_offset, failed;
	int block_frames, block_offset;
	int16_t block[STREAM_BLOCK_FRAMES * 2];
	char failure[128];
};

static const stream_format_t *S_StreamFormat (const char *extension)
{
	size_t i, j;
	if (!extension)
		return NULL;
	for (i = 0; i < sizeof (stream_formats) / sizeof (stream_formats[0]); i++)
	{
		const char *expected = stream_formats[i].extension;
		for (j = 0; expected[j]; j++)
		{
			int c = (unsigned char)extension[j];
			if (c >= 'A' && c <= 'Z')
				c += 'a' - 'A';
			if (c != expected[j])
				break;
		}
		if (!expected[j] && !extension[j])
			return &stream_formats[i];
	}
	return NULL;
}

int S_StreamFormatAvailable (const char *extension)
{
	const stream_format_t *format = S_StreamFormat (extension);
	return format && av_find_input_format (format->demuxer) &&
		(format->codec == AV_CODEC_ID_NONE || avcodec_find_decoder (format->codec));
}

static int S_StreamFileRead (void *opaque, uint8_t *buffer, int size)
{
	snd_stream_t *stream = opaque;
	size_t count;
	int64_t remaining = stream->length - stream->position;
	if (size < 0)
		return AVERROR (EINVAL);
	if (size > remaining)
		size = (int)remaining;
	if (!size)
		return AVERROR_EOF;
	count = fread (buffer, 1, size, stream->file);
	if (!count)
		return ferror (stream->file) ? AVERROR (EIO) : AVERROR_EOF;
	stream->position += count;
	return (int)count;
}

static int64_t S_StreamFileSeekIO (void *opaque, int64_t offset, int whence)
{
	snd_stream_t *stream = opaque;
	int64_t base;
	whence &= ~AVSEEK_FORCE;
	if (whence == AVSEEK_SIZE)
		return stream->length;
	switch (whence)
	{
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = stream->position;
		break;
	case SEEK_END:
		base = stream->length;
		break;
	default:
		return AVERROR (EINVAL);
	}
	if (offset < -base || offset > stream->length - base)
		return AVERROR (EINVAL);
	if (S_StreamFileSeek (stream->file, base + offset, SEEK_SET))
		return AVERROR (EIO);
	stream->position = base + offset;
	return stream->position;
}

void S_StreamClose (snd_stream_t **owner)
{
	snd_stream_t *stream;
	if (!owner || !*owner)
		return;
	stream = *owner;
	*owner = NULL;
	avformat_close_input (&stream->format);
	if (stream->io)
		av_freep (&stream->io->buffer);
	avio_context_free (&stream->io);
	avcodec_free_context (&stream->decoder);
	av_packet_free (&stream->packet);
	av_frame_free (&stream->frame);
	swr_free (&stream->swr);
	av_channel_layout_uninit (&stream->layout);
	av_free (stream);
}

snd_stream_t *S_StreamOpen (FILE *file, const char *extension, int output_rate,
	char *error, int error_size)
{
	const stream_format_t *selected = S_StreamFormat (extension);
	const AVInputFormat *demuxer;
	const AVCodec *codec;
	snd_stream_t *stream = NULL;
	unsigned char *io_buffer = NULL;
	int64_t length;
	int ret;
	const char *failure = NULL;
	S_StreamMessage (error, error_size, "");
	if (!file || !selected || output_rate <= 0 || output_rate > 192000)
	{
		S_StreamMessage (error, error_size, "Invalid music file, extension or output rate");
		return NULL;
	}
	demuxer = av_find_input_format (selected->demuxer);
	if (!demuxer)
	{
		S_StreamMessage (error, error_size, "Music demuxer unavailable in this build");
		return NULL;
	}
	if (S_StreamFileSeek (file, 0, SEEK_END) || (length = S_StreamFileTell (file)) <= 0 ||
		S_StreamFileSeek (file, 0, SEEK_SET))
	{
		S_StreamMessage (error, error_size, "Music requires a nonempty seekable file");
		return NULL;
	}
	stream = av_mallocz (sizeof (*stream));
	if (!stream)
	{
		ret = AVERROR (ENOMEM);
		goto fail;
	}
	stream->file = file;
	stream->length = length;
	stream->output_rate = output_rate;
	stream->format = avformat_alloc_context ();
	io_buffer = av_malloc (4096);
	if (!stream->format || !io_buffer)
	{
		ret = AVERROR (ENOMEM);
		goto fail;
	}
	stream->io = avio_alloc_context (io_buffer, 4096, 0, stream,
		S_StreamFileRead, NULL, S_StreamFileSeekIO);
	if (!stream->io)
	{
		ret = AVERROR (ENOMEM);
		goto fail;
	}
	io_buffer = NULL;
	stream->format->pb = stream->io;
	stream->format->flags |= AVFMT_FLAG_CUSTOM_IO;
	/* No path/URL reaches libavformat; only the caller's real FILE is readable. */
	ret = avformat_open_input (&stream->format, NULL, demuxer, NULL);
	if (ret < 0)
		goto fail;
	ret = avformat_find_stream_info (stream->format, NULL);
	if (ret < 0)
		goto fail;
	ret = av_find_best_stream (stream->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (ret < 0)
		goto fail;
	stream->stream_index = ret;
	codec = avcodec_find_decoder (stream->format->streams[ret]->codecpar->codec_id);
	if (!codec)
	{
		failure = "Music decoder unavailable in this build";
		goto fail;
	}
	stream->decoder = avcodec_alloc_context3 (codec);
	stream->packet = av_packet_alloc ();
	stream->frame = av_frame_alloc ();
	if (!stream->decoder || !stream->packet || !stream->frame)
	{
		ret = AVERROR (ENOMEM);
		goto fail;
	}
	ret = avcodec_parameters_to_context (stream->decoder,
		stream->format->streams[stream->stream_index]->codecpar);
	if (ret < 0)
		goto fail;
	stream->decoder->pkt_timebase = stream->format->streams[stream->stream_index]->time_base;
	stream->decoder->max_samples = STREAM_MAX_SAMPLES;
	stream->decoder->thread_count = 1;
	ret = avcodec_open2 (stream->decoder, codec, NULL);
	if (ret < 0)
		goto fail;
	return stream;

fail:
	if (failure)
		S_StreamMessage (error, error_size, failure);
	else if (error && error_size > 0)
		av_strerror (ret, error, error_size);
	av_free (io_buffer);
	S_StreamClose (&stream);
	return NULL;
}

static int S_StreamDecodeFrame (snd_stream_t *stream)
{
	int ret;
	for (;;)
	{
		ret = avcodec_receive_frame (stream->decoder, stream->frame);
		if (ret != AVERROR (EAGAIN))
		{
			if (ret == AVERROR_EOF && !stream->draining)
				return AVERROR_INVALIDDATA;
			return ret;
		}
		if (stream->draining)
			return AVERROR_INVALIDDATA;
		for (;;)
		{
			ret = av_read_frame (stream->format, stream->packet);
			if (ret < 0 || stream->packet->stream_index == stream->stream_index)
				break;
			av_packet_unref (stream->packet);
		}
		if (ret == AVERROR_EOF)
		{
			stream->draining = 1;
			ret = avcodec_send_packet (stream->decoder, NULL);
		}
		else if (ret >= 0)
		{
			ret = avcodec_send_packet (stream->decoder, stream->packet);
			if (ret >= 0)
				av_packet_unref (stream->packet);
		}
		if (ret < 0)
			return ret;
	}
}

static int S_StreamFrameValid (AVFrame *frame)
{
	int channels = frame->ch_layout.nb_channels;
	if (channels < 1 || channels > STREAM_MAX_CHANNELS || frame->sample_rate <= 0 ||
		frame->nb_samples < 0 || frame->nb_samples > STREAM_MAX_SAMPLES / channels)
		return 0;
	if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
	{
		av_channel_layout_uninit (&frame->ch_layout);
		av_channel_layout_default (&frame->ch_layout, channels);
	}
	return av_channel_layout_check (&frame->ch_layout) && av_get_bytes_per_sample (frame->format) > 0;
}

static int S_StreamConfigure (snd_stream_t *stream)
{
	AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	AVFrame *frame = stream->frame;
	int ret;
	swr_free (&stream->swr);
	av_channel_layout_uninit (&stream->layout);
	ret = av_channel_layout_copy (&stream->layout, &frame->ch_layout);
	if (ret < 0)
		return ret;
	stream->input_rate = frame->sample_rate;
	stream->input_format = frame->format;
	ret = swr_alloc_set_opts2 (&stream->swr, &stereo, AV_SAMPLE_FMT_S16, stream->output_rate,
		&stream->layout, frame->format, frame->sample_rate, 0, NULL);
	if (ret < 0)
		return ret;
	return swr_init (stream->swr);
}

static int S_StreamConvert (snd_stream_t *stream, const uint8_t **input, int frames)
{
	unsigned char *output = (unsigned char *)stream->block;
	const uint16_t little = 1;
	int ret, i;
	ret = swr_convert (stream->swr, &output, STREAM_BLOCK_FRAMES, input, frames);
	if (ret <= 0)
		return ret;
	if (!*(const unsigned char *)&little)
	{
		for (i = 0; i < ret * 4; i += 2)
		{
			unsigned char first = output[i];
			output[i] = output[i + 1];
			output[i + 1] = first;
		}
	}
	stream->block_frames = ret;
	stream->block_offset = 0;
	return ret;
}

static int S_StreamFill (snd_stream_t *stream)
{
	AVFrame *frame = stream->frame;
	const uint8_t *input[STREAM_MAX_CHANNELS], *empty[STREAM_MAX_CHANNELS] = { NULL };
	int ret, changed, channels, width, planar, count, i;
	int64_t preferred;
	for (;;)
	{
		/* A non-NULL empty input drains queued output without flushing the
		 * resampling filter between source frames or caller reads. */
		if (stream->swr)
		{
			ret = S_StreamConvert (stream, empty, 0);
			if (ret)
				return ret;
		}
		if (frame->nb_samples > stream->frame_offset)
		{
			changed = stream->swr && (frame->sample_rate != stream->input_rate || frame->format != stream->input_format || av_channel_layout_compare (&frame->ch_layout, &stream->layout));
			if (changed)
			{
				ret = S_StreamConvert (stream, NULL, 0);
				if (ret)
					return ret;
			}
			if (!stream->swr || changed)
			{
				ret = S_StreamConfigure (stream);
				if (ret < 0)
					return ret;
			}
			/* Feed at most one small source block, adapted to the rate ratio.
			 * Neither a track nor a decoded frame is converted all at once. */
			preferred = av_rescale_rnd (STREAM_BLOCK_FRAMES, frame->sample_rate,
				stream->output_rate, AV_ROUND_UP);
			count = preferred > 1024 ? 1024 : preferred < 1 ? 1 :
															  (int)preferred;
			if (count > frame->nb_samples - stream->frame_offset)
				count = frame->nb_samples - stream->frame_offset;
			width = av_get_bytes_per_sample (frame->format);
			planar = av_sample_fmt_is_planar (frame->format);
			channels = frame->ch_layout.nb_channels;
			for (i = 0; i < (planar ? channels : 1); i++)
			{
				if (!frame->extended_data || !frame->extended_data[i])
					return AVERROR_INVALIDDATA;
				input[i] = frame->extended_data[i] + (size_t)stream->frame_offset * width * (planar ? 1 : channels);
			}
			ret = S_StreamConvert (stream, input, count);
			if (ret < 0)
				return ret;
			stream->frame_offset += count;
			if (stream->frame_offset == frame->nb_samples)
			{
				av_frame_unref (frame);
				stream->frame_offset = 0;
			}
			if (ret)
				return ret;
			continue;
		}
		if (stream->decoder_eof)
			return stream->swr ? S_StreamConvert (stream, NULL, 0) : 0;
		ret = S_StreamDecodeFrame (stream);
		if (ret == AVERROR_EOF)
		{
			stream->decoder_eof = 1;
			continue;
		}
		if (ret < 0)
			return ret;
		if (!S_StreamFrameValid (frame))
			return AVERROR_INVALIDDATA;
		if (!frame->nb_samples)
			av_frame_unref (frame);
	}
}

int S_StreamRead (snd_stream_t *stream, unsigned char *pcm, int frames,
	char *error, int error_size)
{
	int done = 0, available, count, ret;
	S_StreamMessage (error, error_size, "");
	if (!stream || frames < 0 || frames > S_STREAM_MAX_READ_FRAMES || (!pcm && frames))
	{
		S_StreamMessage (error, error_size, "Invalid music read buffer or frame count");
		return -1;
	}
	if (stream->failed)
	{
		S_StreamMessage (error, error_size, stream->failure);
		return -1;
	}
	while (done < frames)
	{
		available = stream->block_frames - stream->block_offset;
		if (!available)
		{
			ret = S_StreamFill (stream);
			if (ret < 0)
			{
				stream->failed = 1;
				av_strerror (ret, stream->failure, sizeof (stream->failure));
				S_StreamMessage (error, error_size, stream->failure);
				return -1;
			}
			if (!ret)
				break;
			available = ret;
		}
		count = frames - done < available ? frames - done : available;
		memcpy (pcm + (size_t)done * 4, (const unsigned char *)stream->block + (size_t)stream->block_offset * 4,
			(size_t)count * 4);
		stream->block_offset += count;
		done += count;
	}
	return done;
}

#else

int S_StreamFormatAvailable (const char *extension)
{
	(void)extension;
	return 0;
}

snd_stream_t *S_StreamOpen (FILE *file, const char *extension, int output_rate,
	char *error, int error_size)
{
	(void)file;
	(void)extension;
	(void)output_rate;
	S_StreamMessage (error, error_size, "File music decoding is unavailable in this build");
	return NULL;
}

int S_StreamRead (snd_stream_t *stream, unsigned char *pcm, int frames,
	char *error, int error_size)
{
	(void)stream;
	(void)pcm;
	(void)frames;
	S_StreamMessage (error, error_size, "File music decoding is unavailable in this build");
	return -1;
}

void S_StreamClose (snd_stream_t **stream)
{
	if (stream)
		*stream = NULL;
}

#endif
