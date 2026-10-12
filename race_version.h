#ifndef RACE_VERSION_H
#define RACE_VERSION_H

#ifdef R1Q2_GENERATED_VERSION
#include <race_version_generated.h>
#else
#define RACE_REVISION "unknown"
#define RACE_COMMIT "unknown"
#define RACE_MODIFIED ""
#define RACE_FILE_VERSION 0,0,0,0
#endif

#define RACE_VERSION RACE_REVISION " ups"
#define RACE_IDENTITY "R1Q2 Race " RACE_VERSION RACE_MODIFIED
#define RACE_DETAILS RACE_IDENTITY " | " RACE_COMMIT

#endif
