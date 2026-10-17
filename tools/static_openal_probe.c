/* Link-only static dependency check. No device or context is opened. */
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#if !defined(_MSC_VER) || (!defined(_M_IX86) && !defined(_M_AMD64)) || defined(_DLL) || defined(_DEBUG)
#error Static OpenAL requires MSVC x86/x64 and the release static CRT (/MT).
#endif
#ifndef AL_LIBTYPE_STATIC
#error OpenAL declarations must use the static library profile.
#endif

static ALCdevice *(ALC_APIENTRY *volatile open_device)(const ALCchar *) = alcOpenDevice;
static void (AL_APIENTRY *volatile source_play)(ALuint) = alSourcePlay;
static void (AL_APIENTRY *volatile buffer_iv)(ALuint, ALenum, const ALint *) = alBufferiv;
static void *(AL_APIENTRY *volatile get_proc)(const ALchar *) = alGetProcAddress;

int main (void)
{
	return !open_device || !source_play || !buffer_iv || !get_proc;
}
