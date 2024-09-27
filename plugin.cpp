#include "plugin.h"
#include "mm/imgui/imgui.h"

//DLLMain code, initializes hook manager
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH) {
		HookMgr::Initialize();
		PluginAttach(hModule, dwReason, lpReserved);
	}

	return TRUE;
}

void PluginAttach(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {} //Will run when plugin attached, placeholder for now

DEFHOOK(void, CPlayer__UpdateController, (void* self, float deltaTime)) { //hooks into CPlayer::UpdateController(), runs before source code

    NoClip(self, deltaTime); //No Clip Plugin

    PositionLoader(self); //Position Loader Plugin

    return CPlayer__UpdateController_orig(self, deltaTime); //runs the rest of CPlayer::UpdateController()
}

void PluginHooks() {
    //Spawner
    ImGuiRenderer::Install();
    PATCHHOOK(ADDRESS(0x1403DB400, 0x141FD4D0C), "\xFF", 1); // veh spawn limit
    PATCHHOOK(ADDRESS(0x1403DB402, 0x141FD4D0E), "\xFF", 1); // ch spawn limit
    MAKENOP(ADDRESS(0x1405CD1C5, 0x142203AEB), 7); // always Vehicle lockedForPlayer = false

    //No Clip
    INSTALLHOOK(ADDRESS(0x1404C6670, 0x1420DEAC0), CPlayer__UpdateController);

    //Intro Videos Skip
    INSTALLHOOK(ADDRESS(0x1405DE290, 0x142215D90), CVideoManager__Update);
}