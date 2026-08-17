#pragma once

#include <iostream>
#include <Windows.h>
#include "mm/hookmgr.h"

#include "mmlib.h"
#include "noclip.h"
#include "positionloader.h"
#include "videoskipplugin.h"
#include "loadstats.h"

extern void* characterVoidPtr;
extern uint16_t systemEventHitCount;

void PluginAttach(HMODULE hModule, DWORD dwReason, LPVOID lpReserved);
void PluginHooks();