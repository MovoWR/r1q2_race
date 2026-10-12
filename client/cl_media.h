/* Private movie decoder. No renderer/client DLL table changes. */
#ifndef CL_MEDIA_H
#define CL_MEDIA_H
#include <stdio.h>
#include <stddef.h>

typedef struct cl_media_s cl_media_t;

typedef struct
{
	int type; /* CL_MEDIA_VIDEO or CL_MEDIA_AUDIO */
	const unsigned char *data;
	size_t size;
	int width, height, stride; /* RGBA8, positive top-down row stride */
	int samples, rate;		   /* stereo S16LE sample pairs */
	double time, duration;	   /* seconds relative to container start */
} cl_media_frame_t;

#define CL_MEDIA_VIDEO 1
#define CL_MEDIA_AUDIO 2
#define CL_MEDIA_EVENT 1
#define CL_MEDIA_AGAIN 2
#define CL_MEDIA_EOF 0
#define CL_MEDIA_ERROR (-1)

int CL_MediaFormatAvailable (const char *extension);
/* Borrows a seekable FILE opened rb; never closes it, even on failure.
 * Rewinds FILE. Caller enforces real-local-file discovery and retains it
 * until close; no other code may operate on it meanwhile. Audio: 1..192000 Hz. */
cl_media_t *CL_MediaOpen (FILE *file, const char *extension, int audio_rate,
	char *error, int error_size);
/* An event's storage lasts until the next Next/Close call. Streams can be
 * interleaved out of timestamp order; the controller owns presentation queues.
 * AGAIN means the bounded work budget expired without output. EOF means both
 * decoders and the resampler drained. Errors are sticky. No device work occurs. */
int CL_MediaNext (cl_media_t *media, cl_media_frame_t *frame,
	char *error, int error_size);
/* Pull only CL_MEDIA_VIDEO or CL_MEDIA_AUDIO. EOF is local to that stream
 * (missing audio is already EOF). Other streams retain compressed packets,
 * not decoded frames. AGAIN may mean the bounded packet queue is full: pull
 * the other stream or use Next to relieve pressure. Both APIs may be mixed. */
int CL_MediaNextStream (cl_media_t *media, int type, cl_media_frame_t *frame,
	char *error, int error_size);
void CL_MediaClose (cl_media_t **media);
#endif
