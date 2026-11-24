#pragma once

// Draws the Dinki overlay by hooking IDXGISwapChain::Present, so it needs no game addresses.
// Used on builds that the legacy address tables don't cover (see dinki_build.h).

#include <Windows.h>

namespace Dinki {

    // Finds Present by creating a throwaway Direct3D 11 device, then hooks it.
    // Must be called from a normal thread, NOT from DllMain (creating a device there can deadlock).
    // Returns false if it couldn't be hooked; it is safe to call again.
    bool InstallPresentHook();

    // The window the game presents to; null until the first frame has been drawn.
    HWND PresentWindow();

    // While blocked, keyboard and mouse messages are not passed on to the game (releases and Alt
    // combinations still are), and a software cursor is drawn. Safe to call from any thread.
    void SetInputBlocked(bool blocked);
    bool IsInputBlocked();

    // Diagnostics.
    unsigned PresentThreadId();
    unsigned long long PresentCount();

    // How long drawing the overlay took inside Present, since the last call (the counters then reset).
    struct OverlayCost {
        unsigned frames = 0;     // frames drawn
        double avgMs = 0.0;
        double maxMs = 0.0;
    };
    OverlayCost TakeOverlayCost();

} // namespace Dinki
