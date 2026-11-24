#include "plugin.h"
#include "mm/imgui/imgui.h"
#include "dinki_watch.h"
#include "dinki_diag.h"
#include "dinki_build.h"
#include "dinki_gog.h"

void* characterVoidPtr = nullptr;
uint16_t systemEventHitCount = 0;

//DLLMain code, initializes hook manager
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH) {
		Dinki::LogBuildDiagnostics(); //records which exe this is (in mm_log.txt) before anything is hooked
		if (Dinki::IsGog()) {
			Dinki::GogStartup(); //a recognized GOG exe: finds what it needs at runtime, no address tables
		}
		else {
			HookMgr::Initialize(); //every other build: the original address tables
		}
		PluginAttach(hModule, dwReason, lpReserved);
	}

	return TRUE;
}

void PluginAttach(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    Log("Plugin successfully connected to Mad Max...");
} //Will run when plugin attached, placeholder for now

DEFHOOK(void, CPlayer__UpdateController, (void* self, float deltaTime)) { //hooks into CPlayer::UpdateController(), runs before source code

    characterVoidPtr = self;

    NoClip(self, deltaTime); //No Clip Plugin

    PositionLoader(self); //Position Loader Plugin

    Dinki::SampleWatches(self); //Watches overlay: reads values here, on the game thread



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