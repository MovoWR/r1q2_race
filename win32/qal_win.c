/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/


// qal_win.c -- binding of AL to QAL function pointers

#ifdef USE_OPENAL

#include "../client/snd_loc.h"

qboolean	openal_active;

ALCOPENDEVICE				qalcOpenDevice;
ALCCLOSEDEVICE				qalcCloseDevice;
ALCCREATECONTEXT			qalcCreateContext;
ALCDESTROYCONTEXT			qalcDestroyContext;
ALCMAKECONTEXTCURRENT		qalcMakeContextCurrent;
ALCPROCESSCONTEXT			qalcProcessContext;
ALCSUSPENDCONTEXT			qalcSuspendContext;
ALCGETCURRENTCONTEXT		qalcGetCurrentContext;
ALCGETCONTEXTSDEVICE		qalcGetContextsDevice;
ALCGETSTRING				qalcGetString;
ALCGETINTEGERV				qalcGetIntegerv;
ALCGETERROR					qalcGetError;
ALCISEXTENSIONPRESENT		qalcIsExtensionPresent;
ALCGETPROCADDRESS			qalcGetProcAddress;
ALCGETENUMVALUE				qalcGetEnumValue;

ALBUFFERDATA				qalBufferData;
ALDELETEBUFFERS				qalDeleteBuffers;
ALDELETESOURCES				qalDeleteSources;
ALDISABLE					qalDisable;
ALDISTANCEMODEL				qalDistanceModel;
ALDOPPLERFACTOR				qalDopplerFactor;
ALDOPPLERVELOCITY			qalDopplerVelocity;
ALENABLE					qalEnable;
ALGENBUFFERS				qalGenBuffers;
ALGENSOURCES				qalGenSources;
ALGETBOOLEAN				qalGetBoolean;
ALGETBOOLEANV				qalGetBooleanv;
ALGETBUFFERF				qalGetBufferf;
ALGETBUFFERI				qalGetBufferi;
ALGETDOUBLE					qalGetDouble;
ALGETDOUBLEV				qalGetDoublev;
ALGETENUMVALUE				qalGetEnumValue;
ALGETERROR					qalGetError;
ALGETFLOAT					qalGetFloat;
ALGETFLOATV					qalGetFloatv;
ALGETINTEGER				qalGetInteger;
ALGETINTEGERV				qalGetIntegerv;
ALGETLISTENER3F				qalGetListener3f;
ALGETLISTENERF				qalGetListenerf;
ALGETLISTENERFV				qalGetListenerfv;
ALGETLISTENERI				qalGetListeneri;
ALGETPROCADDRESS			qalGetProcAddress;
ALGETSOURCE3F				qalGetSource3f;
ALGETSOURCEF				qalGetSourcef;
ALGETSOURCEFV				qalGetSourcefv;
ALGETSOURCEI				qalGetSourcei;
ALGETSTRING					qalGetString;
ALHINT						qalHint;
ALISBUFFER					qalIsBuffer;
ALISENABLED					qalIsEnabled;
ALISEXTENSIONPRESENT		qalIsExtensionPresent;
ALISSOURCE					qalIsSource;
ALLISTENER3F				qalListener3f;
ALLISTENERF					qalListenerf;
ALLISTENERFV				qalListenerfv;
ALLISTENERI					qalListeneri;
ALSOURCE3F					qalSource3f;
ALSOURCEF					qalSourcef;
ALSOURCEFV					qalSourcefv;
ALSOURCEI					qalSourcei;
ALSOURCEPAUSE				qalSourcePause;
ALSOURCEPAUSEV				qalSourcePausev;
ALSOURCEPLAY				qalSourcePlay;
ALSOURCEPLAYV				qalSourcePlayv;
ALSOURCEQUEUEBUFFERS		qalSourceQueueBuffers;
ALSOURCEREWIND				qalSourceRewind;
ALSOURCEREWINDV				qalSourceRewindv;
ALSOURCESTOP				qalSourceStop;
ALSOURCESTOPV				qalSourceStopv;
ALSOURCEUNQUEUEBUFFERS		qalSourceUnqueueBuffers;

ALEAXSET					qalEAXSet;
ALEAXGET					qalEAXGet;


// =====================================================================

#ifdef R1Q2_STATIC_OPENAL
#define QAL_BIND(type, name)	q##name = name
#else
#define QAL_BIND(type, name)	q##name = (type)GetProcAddress(alwState.hInstOpenAL, #name)
#endif


/*
 =================
 QAL_Shutdown

 Unloads the specified DLL then nulls out all the proc pointers
 =================
*/
void QAL_Shutdown (void){

	Com_Printf("...shutting down QAL\n", LOG_CLIENT);

#ifndef R1Q2_STATIC_OPENAL
	if (alwState.hInstOpenAL)
	{
		Com_Printf("...unloading OpenAL DLL\n", LOG_CLIENT);

		FreeLibrary(alwState.hInstOpenAL);
	}
#endif
	alwState.hInstOpenAL = NULL;

	openal_active = false;

	qalcOpenDevice				= NULL;
	qalcCloseDevice				= NULL;
	qalcCreateContext			= NULL;
	qalcDestroyContext			= NULL;
	qalcMakeContextCurrent		= NULL;
	qalcProcessContext			= NULL;
	qalcSuspendContext			= NULL;
	qalcGetCurrentContext		= NULL;
	qalcGetContextsDevice		= NULL;
	qalcGetString				= NULL;
	qalcGetIntegerv				= NULL;
	qalcGetError				= NULL;
	qalcIsExtensionPresent		= NULL;
	qalcGetProcAddress			= NULL;
	qalcGetEnumValue			= NULL;

	qalBufferData				= NULL;
	qalDeleteBuffers			= NULL;
	qalDeleteSources			= NULL;
	qalDisable					= NULL;
	qalDistanceModel			= NULL;
	qalDopplerFactor			= NULL;
	qalDopplerVelocity			= NULL;
	qalEnable					= NULL;
	qalGenBuffers				= NULL;
	qalGenSources				= NULL;
	qalGetBoolean				= NULL;
	qalGetBooleanv				= NULL;
	qalGetBufferf				= NULL;
	qalGetBufferi				= NULL;
	qalGetDouble				= NULL;
	qalGetDoublev				= NULL;
	qalGetEnumValue				= NULL;
	qalGetError					= NULL;
	qalGetFloat					= NULL;
	qalGetFloatv				= NULL;
	qalGetInteger				= NULL;
	qalGetIntegerv				= NULL;
	qalGetListener3f			= NULL;
	qalGetListenerf				= NULL;
	qalGetListenerfv			= NULL;
	qalGetListeneri				= NULL;
	qalGetProcAddress			= NULL;
	qalGetSource3f				= NULL;
	qalGetSourcef				= NULL;
	qalGetSourcefv				= NULL;
	qalGetSourcei				= NULL;
	qalGetString				= NULL;
	qalHint						= NULL;
	qalIsBuffer					= NULL;
	qalIsEnabled				= NULL;
	qalIsExtensionPresent		= NULL;
	qalIsSource					= NULL;
	qalListener3f				= NULL;
	qalListenerf				= NULL;
	qalListenerfv				= NULL;
	qalListeneri				= NULL;
	qalSource3f					= NULL;
	qalSourcef					= NULL;
	qalSourcefv					= NULL;
	qalSourcei					= NULL;
	qalSourcePause				= NULL;
	qalSourcePausev				= NULL;
	qalSourcePlay				= NULL;
	qalSourcePlayv				= NULL;
	qalSourceQueueBuffers		= NULL;
	qalSourceRewind				= NULL;
	qalSourceRewindv			= NULL;
	qalSourceStop				= NULL;
	qalSourceStopv				= NULL;
	qalSourceUnqueueBuffers		= NULL;

	qalEAXSet					= NULL;
	qalEAXGet					= NULL;
}

/*
 =================
 QAL_Init

 Binds our QAL function pointers to the appropriate AL stuff
 =================
*/
qboolean QAL_Init (const char *driver){

#ifndef R1Q2_STATIC_OPENAL
	char	name[MAX_OSPATH], path[MAX_OSPATH];
#endif

	Com_Printf("...initializing QAL\n", LOG_CLIENT);

#ifdef R1Q2_STATIC_OPENAL
	(void)driver;
	Com_Printf ("...using built-in OpenAL\n", LOG_CLIENT);
#else
	Com_sprintf (name, sizeof(name), "%s.dll", driver);

	if (!SearchPath(NULL, name, NULL, sizeof(path), path, NULL))
	{
		Com_Printf("...WARNING: couldn't find OpenAL driver '%s'\n", LOG_CLIENT, name);
		return false;
	}

	Com_Printf("...calling LoadLibrary( '%s' ): ", LOG_CLIENT, path);

	if ((alwState.hInstOpenAL = LoadLibrary(path)) == NULL)
	{
		Com_Printf("failed\n", LOG_CLIENT);
		return false;
	}
	Com_Printf("succeeded\n", LOG_CLIENT);

#endif

	QAL_BIND (ALCOPENDEVICE, alcOpenDevice);
	QAL_BIND (ALCCLOSEDEVICE, alcCloseDevice);
	QAL_BIND (ALCCREATECONTEXT, alcCreateContext);
	QAL_BIND (ALCDESTROYCONTEXT, alcDestroyContext);
	QAL_BIND (ALCMAKECONTEXTCURRENT, alcMakeContextCurrent);
	QAL_BIND (ALCPROCESSCONTEXT, alcProcessContext);
	QAL_BIND (ALCSUSPENDCONTEXT, alcSuspendContext);
	QAL_BIND (ALCGETCURRENTCONTEXT, alcGetCurrentContext);
	QAL_BIND (ALCGETCONTEXTSDEVICE, alcGetContextsDevice);
	QAL_BIND (ALCGETSTRING, alcGetString);
	QAL_BIND (ALCGETINTEGERV, alcGetIntegerv);
	QAL_BIND (ALCGETERROR, alcGetError);
	QAL_BIND (ALCISEXTENSIONPRESENT, alcIsExtensionPresent);
	QAL_BIND (ALCGETPROCADDRESS, alcGetProcAddress);
	QAL_BIND (ALCGETENUMVALUE, alcGetEnumValue);

	QAL_BIND (ALBUFFERDATA, alBufferData);
	QAL_BIND (ALDELETEBUFFERS, alDeleteBuffers);
	QAL_BIND (ALDELETESOURCES, alDeleteSources);
	QAL_BIND (ALDISABLE, alDisable);
	QAL_BIND (ALDISTANCEMODEL, alDistanceModel);
	QAL_BIND (ALDOPPLERFACTOR, alDopplerFactor);
	QAL_BIND (ALDOPPLERVELOCITY, alDopplerVelocity);
	QAL_BIND (ALENABLE, alEnable);
	QAL_BIND (ALGENBUFFERS, alGenBuffers);
	QAL_BIND (ALGENSOURCES, alGenSources);
	QAL_BIND (ALGETBOOLEAN, alGetBoolean);
	QAL_BIND (ALGETBOOLEANV, alGetBooleanv);
	QAL_BIND (ALGETBUFFERF, alGetBufferf);
	QAL_BIND (ALGETBUFFERI, alGetBufferi);
	QAL_BIND (ALGETDOUBLE, alGetDouble);
	QAL_BIND (ALGETDOUBLEV, alGetDoublev);
	QAL_BIND (ALGETENUMVALUE, alGetEnumValue);
	QAL_BIND (ALGETERROR, alGetError);
	QAL_BIND (ALGETFLOAT, alGetFloat);
	QAL_BIND (ALGETFLOATV, alGetFloatv);
	QAL_BIND (ALGETINTEGER, alGetInteger);
	QAL_BIND (ALGETINTEGERV, alGetIntegerv);
	QAL_BIND (ALGETLISTENER3F, alGetListener3f);
	QAL_BIND (ALGETLISTENERF, alGetListenerf);
	QAL_BIND (ALGETLISTENERFV, alGetListenerfv);
	QAL_BIND (ALGETLISTENERI, alGetListeneri);
	QAL_BIND (ALGETPROCADDRESS, alGetProcAddress);
	QAL_BIND (ALGETSOURCE3F, alGetSource3f);
	QAL_BIND (ALGETSOURCEF, alGetSourcef);
	QAL_BIND (ALGETSOURCEFV, alGetSourcefv);
	QAL_BIND (ALGETSOURCEI, alGetSourcei);
	QAL_BIND (ALGETSTRING, alGetString);
#ifndef R1Q2_STATIC_OPENAL
	QAL_BIND (ALHINT, alHint);
#else
	qalHint = NULL;
#endif
	QAL_BIND (ALISBUFFER, alIsBuffer);
	QAL_BIND (ALISENABLED, alIsEnabled);
	QAL_BIND (ALISEXTENSIONPRESENT, alIsExtensionPresent);
	QAL_BIND (ALISSOURCE, alIsSource);
	QAL_BIND (ALLISTENER3F, alListener3f);
	QAL_BIND (ALLISTENERF, alListenerf);
	QAL_BIND (ALLISTENERFV, alListenerfv);
	QAL_BIND (ALLISTENERI, alListeneri);
	QAL_BIND (ALSOURCE3F, alSource3f);
	QAL_BIND (ALSOURCEF, alSourcef);
	QAL_BIND (ALSOURCEFV, alSourcefv);
	QAL_BIND (ALSOURCEI, alSourcei);
	QAL_BIND (ALSOURCEPAUSE, alSourcePause);
	QAL_BIND (ALSOURCEPAUSEV, alSourcePausev);
	QAL_BIND (ALSOURCEPLAY, alSourcePlay);
	QAL_BIND (ALSOURCEPLAYV, alSourcePlayv);
	QAL_BIND (ALSOURCEQUEUEBUFFERS, alSourceQueueBuffers);
	QAL_BIND (ALSOURCEREWIND, alSourceRewind);
	QAL_BIND (ALSOURCEREWINDV, alSourceRewindv);
	QAL_BIND (ALSOURCESTOP, alSourceStop);
	QAL_BIND (ALSOURCESTOPV, alSourceStopv);
	QAL_BIND (ALSOURCEUNQUEUEBUFFERS, alSourceUnqueueBuffers);

	qalEAXGet					= NULL;
	qalEAXSet					= NULL;

	/* These exports are called without optional-extension guards. */
	if (!qalcOpenDevice || !qalcCloseDevice || !qalcCreateContext || !qalcDestroyContext ||
		!qalcMakeContextCurrent || !qalcGetString || !qalcIsExtensionPresent ||
		!qalBufferData || !qalDeleteBuffers || !qalDeleteSources || !qalDistanceModel ||
		!qalDopplerFactor || !qalDopplerVelocity || !qalGenBuffers || !qalGenSources ||
		!qalGetError || !qalGetProcAddress || !qalGetSourcei || !qalGetString ||
		!qalIsExtensionPresent || !qalListenerf || !qalListenerfv || !qalSource3f ||
		!qalSourcef || !qalSourcefv || !qalSourcei || !qalSourcePause || !qalSourcePlay ||
		!qalSourceQueueBuffers || !qalSourceStop || !qalSourceUnqueueBuffers)
	{
		Com_Printf ("Unsupported OpenAL driver '%s' (missing required exports).\n", LOG_CLIENT, driver);
		QAL_Shutdown ();
		return false;
	}

	openal_active = true;

	return true;
}
#endif
