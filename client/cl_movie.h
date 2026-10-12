/* Private replacement movie controller; no public client/renderer ABI changes. */
#ifndef CL_MOVIE_H
#define CL_MOVIE_H

int CL_MovieStart (const char *name);
void CL_MovieStop (void);
int CL_MovieActive (void);
/* Advance from cls.realtime. Returns zero at completion or fatal failure. */
int CL_MovieRun (int paused);
/* Draw cached current frame, reuploading when renderer state was lost. */
void CL_MovieDraw (void);
/* Backend pull: bounded stereo S16LE at rate, lead seconds ahead of movie
 * clock. Returns valid frame count, including timestamp-gap silence.
 * Pause/inactive returns zero. Request limit 8192; no device calls. */
int CL_MovieReadAudio (unsigned char *pcm, int frames, int rate, double lead);
/* Reconcile menu/loading pause before querying generation or backend lead. */
int CL_MoviePaused (void);
/* Changes on start/stop/pause transitions for invalidating backend queues. */
unsigned CL_MovieGeneration (void);
/* Shared stock-name/size matte policy, owned by the retained CIN wrapper. */
void SCR_CinematicCropForFile (const char *name, int size, int *start, int *crop);

#endif
