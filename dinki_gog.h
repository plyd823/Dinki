#pragma once

// The GOG core: everything Dinki needs on the GOG exe (see dinki_build.h) without a table of hard-coded
// function addresses.
//
//   * Overlay        hooks IDXGISwapChain::Present                          (dinki_present.cpp)
//   * Game tick      hooks CMissionManager::UpdateRender, after checking its bytes
//   * Game objects   found at runtime by their vftable pointers             (dinki_scan.cpp)
//   * Diagnostics    everything it finds and does is written to mm_log.txt

#include <cstdint>
#include <string>

namespace Dinki {

    // Called from DllMain on the GOG exe. Installs the tick hook and starts the helper thread that
    // hooks Present and discovers game objects. Returns quickly.
    void GogStartup();

    // Menu opened (true) or closed (false). Stops the game receiving keyboard and mouse input, releases
    // the cursor clip, and clears the game's own "in focus" flag when that object has been found and
    // its layout confirmed. Game thread.
    void GogSetInputGrab(bool grab);

    // The player's CCharacter object, found and verified at runtime (0 until it has been found). Thread safe.
    uintptr_t GogPlayerCharacter();

    // The vehicle the character points at (the one it is in, if it points at several): the START of the vehicle object,
    // found four times a second on the helper thread, so this is cheap enough for every frame. distanceToPlayer is
    // negative when it isn't known.
    bool GogPlayerVehicleInfo(uintptr_t& base, float& distanceToPlayer);
    uintptr_t GogPlayerVehicle();   // the same, just the address (0 if none)

    // Debug text listing every vehicle the character points at, with live health values and the distance to the player.
    std::string GogVehicleCandidatesText();

} // namespace Dinki
