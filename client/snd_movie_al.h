/* Private cinematic sound bridge; no client or renderer DLL ABI changes. */
#ifndef SND_MOVIE_AL_H
#define SND_MOVIE_AL_H

/* Software paint position relative to the most recently sampled DMA clock. */
double S_MoviePaintLead (void);
/* Decoder output rate for the current backend, or 44100 without sound. */
int S_MovieRate (void);
/* Initialize with a current OpenAL context, before allocating effect sources. */
int S_MovieALInit (void);
/* Release before destroying the context; safe when never initialized. */
void S_MovieALShutdown (void);
void S_MovieALUpdate (void);

#endif
