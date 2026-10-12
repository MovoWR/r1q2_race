/* Fileless dependency check; no engine, decoder input, window or audio device. */
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include "snd_ogg.h"
#include "snd_stream.h"
#include "cl_media.h"

#if !defined(_MSC_VER) || !defined(_M_IX86) || defined(_DLL) || defined(_DEBUG)
#error Static FFmpeg requires MSVC x86 and the release static CRT (/MT).
#endif
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 37, 100) || \
 LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 27, 100) || \
 LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 28, 100) || \
 LIBSWRESAMPLE_VERSION_INT < AV_VERSION_INT(4, 7, 100) || \
 LIBSWSCALE_VERSION_INT < AV_VERSION_INT(6, 7, 100)
#error FFmpeg 5.1 or newer headers are required.
#endif

int main (void)
{
	static const char *demuxers[] = { "ogg", "flac", "mp3", "wav", "matroska", "mov", "idcin" };
	static const enum AVCodecID codecs[] = {
		AV_CODEC_ID_VORBIS, AV_CODEC_ID_OPUS, AV_CODEC_ID_FLAC, AV_CODEC_ID_MP3,
		AV_CODEC_ID_THEORA, AV_CODEC_ID_H264, AV_CODEC_ID_AAC, AV_CODEC_ID_IDCIN,
		AV_CODEC_ID_FFV1, AV_CODEC_ID_PCM_U8, AV_CODEC_ID_PCM_S16LE,
		AV_CODEC_ID_PCM_S24LE, AV_CODEC_ID_ADPCM_IMA_WAV
	};
	snd_ogg_t ogg = {0};
	snd_stream_t *stream;
	cl_media_t *media;
	cl_media_frame_t frame;
	char error[160];
	unsigned i;
	int failed = 0;

	if (avcodec_version() != LIBAVCODEC_VERSION_INT ||
		avformat_version() != LIBAVFORMAT_VERSION_INT ||
		avutil_version() != LIBAVUTIL_VERSION_INT ||
		swresample_version() != LIBSWRESAMPLE_VERSION_INT ||
		swscale_version() != LIBSWSCALE_VERSION_INT)
	{
		fprintf (stderr, "FFmpeg library versions do not match the supplied headers.\n");
		return 1;
	}
	for (i = 0; i < sizeof(demuxers) / sizeof(demuxers[0]); i++)
		if (!av_find_input_format(demuxers[i]))
		{
			fprintf (stderr, "Missing FFmpeg demuxer: %s\n", demuxers[i]);
			failed = 1;
		}
	for (i = 0; i < sizeof(codecs) / sizeof(codecs[0]); i++)
		if (!avcodec_find_decoder(codecs[i]))
		{
			fprintf (stderr, "Missing FFmpeg decoder: %s\n", avcodec_get_name(codecs[i]));
			failed = 1;
		}
	/* Reference actual production entry points so their library requirements link. */
	S_DecodeOgg (NULL, 0, &ogg, error, sizeof(error));
	S_FreeOgg (&ogg);
	stream = S_StreamOpen (NULL, ".ogg", 44100, error, sizeof(error));
	S_StreamRead (stream, NULL, 0, error, sizeof(error));
	S_StreamClose (&stream);
	media = CL_MediaOpen (NULL, ".ogv", 44100, error, sizeof(error));
	CL_MediaNext (media, &frame, error, sizeof(error));
	CL_MediaNextStream (media, CL_MEDIA_VIDEO, &frame, error, sizeof(error));
	CL_MediaClose (&media);
	if (!failed)
		printf ("MSVC x86 /MT FFmpeg API, versions and required codec capabilities verified.\n");
	return failed;
}
