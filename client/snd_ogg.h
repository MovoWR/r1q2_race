/* Private sound-asset decoder; no sound/channel or renderer ABI changes. */
#ifndef SND_OGG_H
#define SND_OGG_H

#define S_OGG_MAX_FRAMES 8388607

typedef struct
{
	unsigned char *data;
	int frames, rate, channels;
} snd_ogg_t;

int S_IsOggSound (const char *name, const unsigned char *data, int length);
/* out must be empty (zeroed or previously freed). Success owns interleaved
 * S16LE PCM at the source rate; failure leaves it empty. No filesystem/URL
 * access is performed. The supplied compressed bytes remain caller-owned. */
int S_DecodeOgg (const unsigned char *data, int length, snd_ogg_t *out,
	char *error, int error_size);
void S_FreeOgg (snd_ogg_t *out);

#endif
