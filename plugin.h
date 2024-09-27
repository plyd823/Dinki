#pragma once

#include <iostream>
#include <Windows.h>
#include "mm/hookmgr.h"

//Declaration for hook functions
#define DECLHOOK(type, name, args) extern type (*name##_orig)args; type name##_hook args;
//Install hook function
#define INSTALLHOOK(addr, name) HookMgr::Install(addr, name##_hook, name##_orig)

void PluginAttach(HMODULE hModule, DWORD dwReason, LPVOID lpReserved);
void PluginHooks();