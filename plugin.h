#pragma once

#include <iostream>
#include <Windows.h>
#include "mm/hookmgr.h"

#include "mmlib.h"
#include "noclip.h"
#include "positionloader.h"
#include "videoskipplugin.h"

void PluginAttach(HMODULE hModule, DWORD dwReason, LPVOID lpReserved);
void PluginHooks();