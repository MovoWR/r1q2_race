/* Private disk-music controller. Does not change the renderer/client DLL ABI. */
#ifndef SND_MUSIC_H
#define SND_MUSIC_H

void S_MusicInit (void);
void S_MusicShutdown (void);
void S_MusicPlay (void);
void S_MusicBeginCinematic (void);
void S_MusicResume (void);
void S_MusicStop (void);
void S_MusicGameDirChanged (void);
void S_MusicUpdate (void);
/* Pull at most 8192 stereo S16LE frames. Only returned frames are valid.
 * The backend owns buffering; never submit beyond its available capacity. */
int S_MusicRead (unsigned char *pcm, int frames, int rate);
float S_MusicVolume (void);
int S_MusicPaused (void);
/* Changes on explicit replacement/stop, not natural track transitions. */
unsigned S_MusicGeneration (void);

#endif
