/* Private OpenAL music queue; no client or renderer DLL ABI changes. */
#ifndef SND_MUSIC_AL_H
#define SND_MUSIC_AL_H

/* Call with a current OpenAL context, before allocating effect channels. */
int S_MusicALInit (void);
/* Call before destroying the OpenAL context. Safe when never initialized. */
void S_MusicALShutdown (void);
/* Call even during loading/cinematics so the private source can pause. */
void S_MusicALUpdate (void);

#endif
