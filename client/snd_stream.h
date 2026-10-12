/* Private pull decoder for real-local-file music, independent of sound output. */
#ifndef SND_STREAM_H
#define SND_STREAM_H

#include <stdio.h>

#define S_STREAM_MAX_READ_FRAMES 8192
typedef struct snd_stream_s snd_stream_t;

int S_StreamFormatAvailable (const char *extension);
/* Borrows a seekable binary FILE (opened with "rb") until close; never closes
 * it, even on failure.
 * Open rewinds it. Do not operate on the FILE while the decoder is open.
 * extension includes its dot. Output is stereo S16LE at 1..192000 Hz. */
snd_stream_t *S_StreamOpen (FILE *file, const char *extension, int output_rate,
	char *error, int error_size);
/* Returns frames produced, 0 at fully drained EOF, or -1 on failure.
 * Requests must be 0..S_STREAM_MAX_READ_FRAMES; zero does not consume input.
 * Discard output on -1. Decode failures are sticky until close. Invalid call
 * arguments do not consume input. Caller provides frames * 4 writable bytes. */
int S_StreamRead (snd_stream_t *stream, unsigned char *pcm, int frames,
	char *error, int error_size);
void S_StreamClose (snd_stream_t **stream);

#endif
