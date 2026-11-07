// The exe's import address table slots the source calls Win32 through (Bink's are port/src/library/bink.cpp's).

#include <windows.h>
#include <dsound.h>
#include <mmsystem.h>

#include "thug2web/imports.h"

IMPORT_SLOT(0x0064502C, DirectSoundEnumerateA);
IMPORT_SLOT(0x006450BC, WaitForSingleObject);
IMPORT_SLOT(0x006450C0, ReleaseMutex);
IMPORT_SLOT(0x00645224, MessageBoxA);
IMPORT_SLOT(0x00645264, timeSetEvent);
IMPORT_SLOT(0x00645268, timeKillEvent);
