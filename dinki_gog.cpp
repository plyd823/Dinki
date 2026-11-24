#include "dinki_gog.h"
#include "plugin.h"
#include "mm/imgui/imgui.h"
#include "dinki_build.h"
#include "dinki_cmd.h"
#include "dinki_player.h"
#include "dinki_present.h"
#include "dinki_scan.h"
#include "dinki_watch.h"

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace {

    // ------------------------------------------------------------------------------------------
    // Facts about THE GOG EXE (PE timestamp 0x565D5965), taken from the Ghidra export.
    // ------------------------------------------------------------------------------------------

    // NMissionSystem::CMissionManager::UpdateRender(float): a large function that runs every frame.
    constexpr uintptr_t kUpdateRender = 0x140468FA0;
    const uint8_t kUpdateRenderBytes[16] = { 0x40, 0x55, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xA8, 0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x58, 0x03 };

    struct KnownClass {
        const char* name;
        uintptr_t vftable;
    };

    // vftable addresses (RTTI labels in Ghidra). Objects are found by looking for these at runtime.
    const KnownClass kClasses[] = {
        { "CMissionManager (A)",            0x14121E460 },
        { "CMissionManager (B)",            0x14121E478 },
        { "CMission",                       0x14121B640 },
        { "CCharacterManager (A)",          0x1411C1660 },
        { "CCharacterManager (B)",          0x1411C1690 },
        { "CGameState",                     0x1411F7558 },
        { "CWorldTime",                     0x14126D5C0 },
        { "CRelicSystem",                   0x1412150F0 },
        { "CEncampmentInventoryManager",    0x1411CAA90 },
        { "CUpgradeableValuesManager (A)",  0x14120E180 },
        { "CUpgradeableValuesManager (B)",  0x14120E198 },
        { "CGameWorld (A)",                 0x14120E750 },
        { "CGameWorld (B)",                 0x14120E788 },
        { "CCameraControlManager",          0x1412508E0 },
        { "Input::CInputDeviceManager",     0x14129E198 },
    };

    constexpr uintptr_t kMissionManagerA = 0x14121E460;
    constexpr uintptr_t kMissionManagerB = 0x14121E478;
    constexpr uintptr_t kMissionVftable = 0x14121B640;
    constexpr uintptr_t kInputManagerVftable = 0x14129E198;

    // Input::CInputDeviceManager layout (from Dinki's input.h): vftable at +0, DirectInput8 at +0x70,
    // the window handle at +0x78, and the bool m_IsInFocus at +0x80.
    constexpr size_t kInputWindowOffset = 0x78;
    constexpr size_t kInputFocusOffset = 0x80;

    // Where inside a heap object a vftable pointer might sit (first base, later bases, and a class that
    // is embedded 0x70 / 0xA0 bytes into the device manager).
    const std::vector<size_t> kVftableOffsets = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x70, 0xA0 };

    constexpr size_t kManagerScanBytes = 0x800;

    // The player. CPlayer has one vftable per base class and holds a pointer to the CCharacter it controls (at +0x10)
    // and a pointer back to itself (at +0x18), per Dinki's CPlayer struct. CCharacter and the vehicle classes also have
    // one vftable per base class; an object starts with one of them.
    const std::vector<uintptr_t> kPlayerVftables = { 0x141231A10, 0x141231A38, 0x141231B10 };
    const std::vector<uintptr_t> kCharacterVftables = { 0x1411C0B40, 0x1411C0B58, 0x1411C0B78, 0x1411C0C08, 0x1411C0C28, 0x1411C0DA8 };
    const std::vector<uintptr_t> kVehicleVftables = { 0x141249810, 0x141249828, 0x141249A38, 0x141249A58,    // CCar
                                                      0x141247550, 0x141247568, 0x1412476E8 };                // CVehicle

    constexpr size_t kCharacterScanBytes = 0x2000;

    constexpr uintptr_t kCharacterManagerA = 0x1411C1660;
    constexpr uintptr_t kCharacterManagerB = 0x1411C1690;

    // The player is found by scanning the game's memory, which is the one part of the GOG core that can cost
    // real performance. This switch (also a checkbox in the menu, and "gog.player_scan" in dinki.ini) turns it off.
    const int kPlayerScanSetting = Dinki::RegisterBoolSetting("gog.player_scan",
        "Find the player (reads the game's memory)", "GOG core", true);

    constexpr size_t kScanSliceBytes = 32u << 20;   // read 32 MB, then pause, so the game keeps its memory bandwidth
    constexpr unsigned kMinHealthyFrames = 15;      // below this many frames/s the scan waits (loading, cutscenes, a struggling game)
    constexpr unsigned kMinHealthyTicks = 5;        // ... or this many ticks/s (the game thread is barely running)

    // ------------------------------------------------------------------------------------------
    // Logging (Log() isn't thread-safe and we log from three threads)
    // ------------------------------------------------------------------------------------------

    void GogLog(const char* format, ...) {
        static std::mutex mutex;
        char buffer[1024];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        std::lock_guard<std::mutex> lock(mutex);
        Log("%s", buffer);
    }

    std::string Fmt(const char* format, ...) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return buffer;
    }

    // ------------------------------------------------------------------------------------------
    // Safe memory reads: never faults, never touches guard pages
    // ------------------------------------------------------------------------------------------

    // A few recently queried memory regions, so reading many pointers into the same heap pool doesn't need a
    // VirtualQuery call (slow in a process with this many regions) for every one of them.
    struct RegionEntry {
        uintptr_t begin = 0;
        uintptr_t end = 0;
        bool readable = false;
    };
    struct RegionCache {
        static constexpr int kEntries = 64;
        RegionEntry entries[kEntries];
        int count = 0;
        int next = 0;
    };
    thread_local RegionCache t_cache;

    bool CopyChecked(void* out, uintptr_t source, size_t size) {
#if defined(_MSC_VER)
        __try {
            std::memcpy(out, reinterpret_cast<const void*>(source), size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
#else
        SIZE_T got = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(source), out, size, &got) && got == size;
#endif
    }

    void ResetReadCache() {
        t_cache.count = 0;
        t_cache.next = 0;
    }

    bool SafeRead(uintptr_t address, void* out, size_t size) {
        if (address < 0x10000 || address + size < address)
            return false;

        RegionEntry entry;
        bool cached = false;
        for (int i = 0; i < t_cache.count; ++i) {
            const RegionEntry& e = t_cache.entries[i];
            if (address >= e.begin && address < e.end) {
                entry = e;
                cached = true;
                break;
            }
        }

        if (!cached) {
            MEMORY_BASIC_INFORMATION info = {};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)))
                return false;
            entry.begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
            entry.end = entry.begin + info.RegionSize;
            const DWORD protect = info.Protect;
            entry.readable = info.State == MEM_COMMIT &&
                !(protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            t_cache.entries[t_cache.next] = entry;
            t_cache.next = (t_cache.next + 1) % RegionCache::kEntries;
            if (t_cache.count < RegionCache::kEntries)
                ++t_cache.count;
        }

        if (address + size > entry.end)
            return false;               // crosses into another region: just fail
        return entry.readable && CopyChecked(out, address, size);
    }

    template <class T>
    bool SafeReadValue(uintptr_t address, T& out) { return SafeRead(address, &out, sizeof(T)); }

    // For the GAME thread, which reads a handful of values every frame: a plain copy that survives an access
    // violation. SafeRead asks Windows about the memory first (VirtualQuery), and system calls can be very slow on
    // a machine with security software; this makes none. Only use it on addresses that were already validated.
    bool FastRead(uintptr_t address, void* out, size_t size) {
        if (address < 0x10000 || address + size < address)
            return false;
        return CopyChecked(out, address, size);
    }

    template <class T>
    bool FastReadValue(uintptr_t address, T& out) { return FastRead(address, &out, sizeof(T)); }

    void HexDump(const char* title, uintptr_t address, size_t size) {
        GogLog("%s (0x%llX):", title, static_cast<unsigned long long>(address));
        for (size_t offset = 0; offset < size; offset += 16) {
            uint8_t bytes[16] = {};
            const size_t n = size - offset < 16 ? size - offset : 16;
            if (!SafeRead(address + offset, bytes, n)) {
                GogLog("    +%03zX  (unreadable)", offset);
                return;
            }
            char line[128];
            int length = std::snprintf(line, sizeof(line), "    +%03zX ", offset);
            for (size_t i = 0; i < n; ++i)
                length += std::snprintf(line + length, sizeof(line) - length, " %02X", bytes[i]);
            GogLog("%s", line);
        }
    }

    // ------------------------------------------------------------------------------------------
    // The exe's own sections
    // ------------------------------------------------------------------------------------------

    struct Sections {
        Dinki::Scan::Range image, data, rdata;
        bool ok = false;
    };

    Sections ReadSections() {
        Sections result;
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!base)
            return result;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        result.image = { base, base + nt->OptionalHeader.SizeOfImage };
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            char name[9] = {};
            std::memcpy(name, section->Name, 8);
            const Dinki::Scan::Range range = { base + section->VirtualAddress, base + section->VirtualAddress + section->Misc.VirtualSize };
            if (std::strcmp(name, ".data") == 0) result.data = range;
            if (std::strcmp(name, ".rdata") == 0) result.rdata = range;
        }
        result.ok = result.data.end > result.data.begin && result.rdata.end > result.rdata.begin;
        return result;
    }

    // ------------------------------------------------------------------------------------------
    // What has been discovered (written by the helper thread, read by the game thread)
    // ------------------------------------------------------------------------------------------

    struct Results {
        uintptr_t missionManager = 0;
        bool missionManagerConfirmed = false;   // both of its vftables were found in the same object
        bool haveMissionVector = false;
        size_t missionVectorOffset = 0;
        uintptr_t inputManager = 0;             // layout confirmed: the window handle field matches the game window
        uintptr_t characterManager = 0;         // only used for a log report on how it refers to the player
    };

    std::mutex g_resultsMutex;
    Results g_results;

    Results GetResults() {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        return g_results;
    }

    // ------------------------------------------------------------------------------------------
    // Game tick
    // ------------------------------------------------------------------------------------------

    std::atomic<unsigned long long> g_tickCount{ 0 };
    std::atomic<unsigned> g_tickThread{ 0 };
    std::atomic<unsigned> g_ticksPerSecond{ 0 };
    std::atomic<unsigned> g_framesPerSecond{ 0 };

    // what our own code costs, refreshed once a second (microseconds)
    std::atomic<unsigned> g_tickCostAvgUs{ 0 };
    std::atomic<unsigned> g_tickCostMaxUs{ 0 };
    std::atomic<unsigned> g_overlayCostAvgUs{ 0 };
    std::atomic<unsigned> g_overlayCostMaxUs{ 0 };
    std::atomic<unsigned> g_watchCostAvgUs{ 0 };      // part of our tick hook: sampling the watches
    std::atomic<unsigned> g_processCostAvgUs{ 0 };    // part of our tick hook: commands and the other renderers

    // Is the game running well enough that a background scan won't make things worse?
    bool GameIsHealthy() {
        return g_framesPerSecond.load(std::memory_order_relaxed) >= kMinHealthyFrames &&
               g_ticksPerSecond.load(std::memory_order_relaxed) >= kMinHealthyTicks;
    }

    void OnTick() {
        LARGE_INTEGER started;
        QueryPerformanceCounter(&started);

        const unsigned long long count = g_tickCount.fetch_add(1, std::memory_order_relaxed) + 1;
        g_tickThread.store(GetCurrentThreadId(), std::memory_order_relaxed);

        // rates and costs, refreshed once a second
        static ULONGLONG windowStart = GetTickCount64();
        static unsigned long long windowTicks = 0, windowFrames = 0;
        static unsigned long long costTotal = 0, costMax = 0;   // QueryPerformanceCounter ticks spent in this function
        static unsigned long long watchTotal = 0, processTotal = 0;   // ... and how that splits
        static const LARGE_INTEGER frequency = []() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();

        const ULONGLONG now = GetTickCount64();
        if (now - windowStart >= 1000) {
            const unsigned long long frames = Dinki::PresentCount();
            const unsigned long long ticksThisWindow = count - windowTicks;
            g_ticksPerSecond.store(static_cast<unsigned>(ticksThisWindow), std::memory_order_relaxed);
            g_framesPerSecond.store(static_cast<unsigned>(frames - windowFrames), std::memory_order_relaxed);
            if (ticksThisWindow) {
                const unsigned long long perSecond = static_cast<unsigned long long>(frequency.QuadPart);
                g_tickCostAvgUs.store(static_cast<unsigned>(costTotal * 1000000ull / perSecond / ticksThisWindow), std::memory_order_relaxed);
                g_tickCostMaxUs.store(static_cast<unsigned>(costMax * 1000000ull / perSecond), std::memory_order_relaxed);
                g_watchCostAvgUs.store(static_cast<unsigned>(watchTotal * 1000000ull / perSecond / ticksThisWindow), std::memory_order_relaxed);
                g_processCostAvgUs.store(static_cast<unsigned>(processTotal * 1000000ull / perSecond / ticksThisWindow), std::memory_order_relaxed);
            }
            const Dinki::OverlayCost overlay = Dinki::TakeOverlayCost();
            g_overlayCostAvgUs.store(static_cast<unsigned>(overlay.avgMs * 1000.0), std::memory_order_relaxed);
            g_overlayCostMaxUs.store(static_cast<unsigned>(overlay.maxMs * 1000.0), std::memory_order_relaxed);

            costTotal = costMax = watchTotal = processTotal = 0;
            windowTicks = count;
            windowFrames = frames;
            windowStart = now;
        }

        ResetReadCache();
        Dinki::SampleWatches(nullptr);
        LARGE_INTEGER sampled;
        QueryPerformanceCounter(&sampled);

        ImGuiRenderer::ProcessGame();

        LARGE_INTEGER finished;
        QueryPerformanceCounter(&finished);
        const unsigned long long cost = static_cast<unsigned long long>(finished.QuadPart - started.QuadPart);
        costTotal += cost;
        if (cost > costMax)
            costMax = cost;
        watchTotal += static_cast<unsigned long long>(sampled.QuadPart - started.QuadPart);
        processTotal += static_cast<unsigned long long>(finished.QuadPart - sampled.QuadPart);
    }

    DEFHOOK(void, GogUpdateRender, (void* self, float dt)) {
        GogUpdateRender_orig(self, dt);
        OnTick();
    }

    bool InstallTickHook() {
        uint8_t actual[16] = {};
        if (!SafeRead(kUpdateRender, actual, sizeof(actual))) {
            GogLog("GOG core: cannot read UpdateRender at 0x%llX; no tick hook", static_cast<unsigned long long>(kUpdateRender));
            return false;
        }
        if (std::memcmp(actual, kUpdateRenderBytes, sizeof(actual)) != 0) {
            char text[64] = {};
            for (int i = 0; i < 16; ++i)
                std::snprintf(text + i * 3, sizeof(text) - i * 3, "%02X ", actual[i]);
            GogLog("GOG core: bytes at UpdateRender (0x%llX) are not what this build should have, so it will NOT be patched.", static_cast<unsigned long long>(kUpdateRender));
            GogLog("          found: %s", text);
            return false;
        }

        MH_STATUS status = MH_CreateHook(reinterpret_cast<LPVOID>(kUpdateRender), reinterpret_cast<LPVOID>(&GogUpdateRender_hook),
            reinterpret_cast<LPVOID*>(&GogUpdateRender_orig));
        if (status != MH_OK) {
            GogLog("GOG core: MH_CreateHook failed for UpdateRender (MinHook status %d)", static_cast<int>(status));
            return false;
        }
        status = MH_EnableHook(reinterpret_cast<LPVOID>(kUpdateRender));
        if (status != MH_OK) {
            GogLog("GOG core: MH_EnableHook failed for UpdateRender (MinHook status %d)", static_cast<int>(status));
            return false;
        }
        GogLog("GOG core: tick hook installed on CMissionManager::UpdateRender (0x%llX)", static_cast<unsigned long long>(kUpdateRender));
        return true;
    }

    // ------------------------------------------------------------------------------------------
    // Discovery (helper thread)
    // ------------------------------------------------------------------------------------------

    const char* ClassName(uintptr_t vftable) {
        for (const KnownClass& c : kClasses)
            if (c.vftable == vftable)
                return c.name;
        return "?";
    }

    std::string g_lastSignature;
    int g_vectorFailures = 0;
    bool g_loggedManagerDump = false;
    bool g_loggedMissionDump = false;

    // Returns true once everything we look for has been found.
    bool DiscoveryPass(int pass) {
        const Sections sections = ReadSections();
        if (!sections.ok) {
            GogLog("GOG core: could not read the exe's sections");
            return false;
        }
        ResetReadCache();
        const Dinki::Scan::ReadFn read = [](uintptr_t address, void* out, size_t size) { return SafeRead(address, out, size); };

        std::vector<uintptr_t> vftables;
        for (const KnownClass& c : kClasses)
            vftables.push_back(c.vftable);

        const ULONGLONG started = GetTickCount64();
        const std::vector<Dinki::Scan::MultiHit> hits = Dinki::Scan::FindObjectsMulti(read, sections.data, sections.image, vftables, kVftableOffsets);
        const ULONGLONG elapsed = GetTickCount64() - started;

        // Report what was found, but only when it changed since the last pass.
        std::string signature;
        for (const auto& h : hits)
            signature += Fmt("%llX:%llX:%zX;", static_cast<unsigned long long>(h.vftable), static_cast<unsigned long long>(h.hit.base), h.hit.vftableOffset);
        if (signature != g_lastSignature) {
            g_lastSignature = signature;
            GogLog("GOG core: discovery pass %d scanned .data in %llu ms, %zu object hit(s):", pass, elapsed, hits.size());
            for (const auto& h : hits)
                GogLog("    %-32s object 0x%llX  vftable at +0x%zX  %s  refs %d", ClassName(h.vftable),
                    static_cast<unsigned long long>(h.hit.base), h.hit.vftableOffset,
                    h.hit.isStatic ? "(static, inside .data)" : "(heap)", h.hit.refs);
            for (const KnownClass& c : kClasses) {
                bool seen = false;
                for (const auto& h : hits) seen = seen || h.vftable == c.vftable;
                if (!seen)
                    GogLog("    %-32s not found yet", c.name);
            }
        }

        Results results = GetResults();

        // ---- mission manager: prefer an object that holds BOTH of its vftables ----
        if (!results.missionManager) {
            for (const auto& a : hits) {
                if (a.vftable != kMissionManagerA || a.hit.isStatic)
                    continue;
                for (const auto& b : hits)
                    if (b.vftable == kMissionManagerB && b.hit.base == a.hit.base) {
                        results.missionManager = a.hit.base;
                        results.missionManagerConfirmed = true;
                        GogLog("GOG core: CMissionManager = 0x%llX (both vftables found in this object: A at +0x%zX, B at +0x%zX)",
                            static_cast<unsigned long long>(a.hit.base), a.hit.vftableOffset, b.hit.vftableOffset);
                        break;
                    }
                if (results.missionManager)
                    break;
            }
            if (!results.missionManager) {
                for (const auto& h : hits)
                    if ((h.vftable == kMissionManagerA || h.vftable == kMissionManagerB) && !h.hit.isStatic && h.hit.vftableOffset == 0) {
                        results.missionManager = h.hit.base;
                        GogLog("GOG core: CMissionManager = 0x%llX (only one of its two vftables seen; not confirmed)", static_cast<unsigned long long>(h.hit.base));
                        break;
                    }
            }
        }

        // ---- the character manager (a log report later shows how it refers to the player) ----
        if (!results.characterManager) {
            for (const auto& a : hits) {
                if (a.vftable != kCharacterManagerA || a.hit.isStatic)
                    continue;
                for (const auto& b : hits)
                    if (b.vftable == kCharacterManagerB && b.hit.base == a.hit.base) {
                        results.characterManager = a.hit.base;
                        GogLog("GOG core: CCharacterManager = 0x%llX", static_cast<unsigned long long>(a.hit.base));
                        break;
                    }
                if (results.characterManager)
                    break;
            }
        }

        // ---- the mission list: a vector of pointers to CMission objects ----
        if (results.missionManager && !results.haveMissionVector) {
            if (!g_loggedManagerDump) {
                HexDump("CMissionManager object, first 0x100 bytes", results.missionManager, 0x100);
                g_loggedManagerDump = true;
            }
            const auto isMission = [](uint64_t vftable) { return vftable == kMissionVftable; };
            const auto vectors = Dinki::Scan::FindPointerVectors(read, results.missionManager, kManagerScanBytes, isMission);
            if (!vectors.empty()) {
                results.haveMissionVector = true;
                results.missionVectorOffset = vectors[0].offset;
                GogLog("GOG core: mission list found at manager+0x%zX: %zu missions, array at 0x%llX",
                    vectors[0].offset, vectors[0].count, static_cast<unsigned long long>(vectors[0].begin));
                for (size_t i = 1; i < vectors.size() && i < 4; ++i)
                    GogLog("          other vector of CMission pointers at manager+0x%zX (%zu elements)", vectors[i].offset, vectors[i].count);

                if (!g_loggedMissionDump) {
                    uint64_t first = 0;
                    if (SafeReadValue(vectors[0].begin, first))
                        HexDump("first CMission object, first 0x100 bytes", static_cast<uintptr_t>(first), 0x100);
                    g_loggedMissionDump = true;
                }
            }
            else if (g_vectorFailures++ < 3) {
                GogLog("GOG core: no vector of CMission pointers in the first 0x%zX bytes of the manager (attempt %d)", kManagerScanBytes, g_vectorFailures);
                // Show what vector-like things ARE there: any vector whose elements start with a vftable in .rdata
                const Dinki::Scan::Range rdata = sections.rdata;
                const auto anyVftable = [rdata](uint64_t v) { return rdata.Contains(static_cast<uintptr_t>(v)); };
                const auto others = Dinki::Scan::FindPointerVectors(read, results.missionManager, kManagerScanBytes, anyVftable);
                for (size_t i = 0; i < others.size() && i < 5; ++i) {
                    uint64_t firstElement = 0, firstVftable = 0;
                    SafeReadValue(others[i].begin, firstElement);
                    SafeReadValue(static_cast<uintptr_t>(firstElement), firstVftable);
                    GogLog("          vector at manager+0x%zX: %zu elements, first element's vftable 0x%llX", others[i].offset, others[i].count,
                        static_cast<unsigned long long>(firstVftable));
                }
            }
        }

        // ---- the game's input manager (only once we know the game window, to confirm the layout) ----
        const HWND window = Dinki::PresentWindow();
        if (!results.inputManager && window) {
            for (const auto& h : hits) {
                if (h.vftable != kInputManagerVftable)
                    continue;
                const uintptr_t manager = h.hit.base + h.hit.vftableOffset;
                uint64_t handle = 0;
                if (SafeReadValue(manager + kInputWindowOffset, handle) && handle == reinterpret_cast<uint64_t>(window)) {
                    results.inputManager = manager;
                    GogLog("GOG core: input manager = 0x%llX. Its window-handle field matches the game window, so the layout is confirmed.", static_cast<unsigned long long>(manager));
                    break;
                }
                GogLog("GOG core: a candidate input manager at 0x%llX has window field 0x%llX, which is not the game window 0x%p; ignoring it",
                    static_cast<unsigned long long>(manager), static_cast<unsigned long long>(handle), static_cast<void*>(window));
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_resultsMutex);
            g_results = results;
        }
        return results.missionManager && results.haveMissionVector && results.inputManager;
    }

    // ------------------------------------------------------------------------------------------
    // The player (helper thread)
    // ------------------------------------------------------------------------------------------

    struct PlayerState {
        uintptr_t player = 0;
        uintptr_t character = 0;
    };

    std::mutex g_playerMutex;
    PlayerState g_player;

    PlayerState GetPlayer() {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        return g_player;
    }

    void SetPlayer(const PlayerState& state) {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = state;
    }

    bool IsVehicleVftable(uint64_t vftable) {
        for (uintptr_t v : kVehicleVftables)
            if (v == vftable)
                return true;
        return false;
    }

    // Every committed, readable, private (heap or virtual-alloc) region of the process. The player isn't
    // referenced from the exe's data, so finding it means looking through the heap.
    std::vector<Dinki::Scan::MemoryRegion> EnumerateScanRegions() {
        std::vector<Dinki::Scan::MemoryRegion> regions;
        SYSTEM_INFO system = {};
        GetSystemInfo(&system);
        uintptr_t address = reinterpret_cast<uintptr_t>(system.lpMinimumApplicationAddress);
        const uintptr_t limit = reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress);

        while (address < limit) {
            MEMORY_BASIC_INFORMATION info = {};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)))
                break;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
            const uintptr_t end = begin + info.RegionSize;
            const DWORD protect = info.Protect;
            const bool readable = (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                              PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
            if (info.State == MEM_COMMIT && info.Type == MEM_PRIVATE && readable &&
                !(protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_NOCACHE | PAGE_WRITECOMBINE)))
                regions.push_back({ begin, end });
            if (end <= address)
                break;
            address = end;
        }
        return regions;
    }

    // The same, but only the pages that are already in the process's working set. Reading a page that was never
    // touched makes Windows allocate and zero real memory for it; a scan of the whole heap must not do that.
    std::vector<Dinki::Scan::MemoryRegion> EnumerateResidentRegions() {
        SYSTEM_INFO system = {};
        GetSystemInfo(&system);
        const size_t pageSize = system.dwPageSize;

        const Dinki::Scan::ResidencyFn query = [pageSize](uintptr_t firstPage, size_t pageCount, std::vector<bool>& resident) {
            std::vector<PSAPI_WORKING_SET_EX_INFORMATION> info(pageCount);
            for (size_t i = 0; i < pageCount; ++i)
                info[i].VirtualAddress = reinterpret_cast<PVOID>(firstPage + i * pageSize);
            if (!QueryWorkingSetEx(GetCurrentProcess(), info.data(), static_cast<DWORD>(pageCount * sizeof(info[0]))))
                return;                                 // can't tell: report nothing as resident
            for (size_t i = 0; i < pageCount; ++i)
                resident[i] = info[i].VirtualAttributes.Valid != 0;
        };
        return Dinki::Scan::KeepResidentPages(EnumerateScanRegions(), pageSize, 256, query);
    }

    // One-off report on a newly found character: what the OLD layout's offsets contain, plus heuristic candidates,
    // so a wrong offset is easy to spot and correct from the log.
    void AnalyseCharacter(const Dinki::Scan::ReadFn& read, const PlayerState& state) {
        HexDump("CPlayer object, first 0x60 bytes", state.player, 0x60);

        const uintptr_t c = state.character;
        auto f32 = [c](size_t offset, float& out) { return SafeReadValue(c + offset, out); };

        float maxHealth = 0, curHealth = 0;
        if (f32(0x180, maxHealth) && f32(0x184, curHealth))
            GogLog("player: at the old offsets  health (+0x180 / +0x184) = %.2f / %.2f", curHealth, maxHealth);

        float px = 0, py = 0, pz = 0;
        if (f32(0x208, px) && f32(0x20C, py) && f32(0x210, pz))
            GogLog("player: at the old offsets  position (+0x208..) = %.3f, %.3f, %.3f", px, py, pz);

        float vx = 0, vy = 0, vz = 0, speed = 0;
        if (f32(0x378, vx) && f32(0x37C, vy) && f32(0x380, vz) && f32(0x384, speed))
            GogLog("player: at the old offsets  velocity (+0x378..) = %.3f, %.3f, %.3f   speed (+0x384) = %.3f", vx, vy, vz, speed);

        int8_t state8 = 0;
        if (SafeReadValue(c + 0x17D0, state8))
            GogLog("player: at the old offsets  motion state (+0x17D0) = %d", static_cast<int>(state8));

        const auto pairs = Dinki::Player::FindHealthLikePairs(read, c, kCharacterScanBytes, 12);
        GogLog("player: health-like (max, current) pairs in the first 0x%zX bytes (up to 12):", kCharacterScanBytes);
        for (const auto& p : pairs)
            GogLog("    +0x%03zX   max %.3f   current %.3f", p.offset, p.first, p.second);

        const auto triples = Dinki::Player::FindPositionLikeTriples(read, c, kCharacterScanBytes, 16);
        GogLog("player: position-like float triples in the first 0x%zX bytes (up to 16):", kCharacterScanBytes);
        for (const auto& t : triples)
            GogLog("    +0x%03zX   %.3f  %.3f  %.3f", t.offset, t.x, t.y, t.z);

        const auto cars = Dinki::Scan::FindPointerFields(read, c, kCharacterScanBytes, IsVehicleVftable);
        GogLog("player: fields holding a pointer to a vehicle object: %zu", cars.size());
        for (size_t offset : cars)
            GogLog("    +0x%03zX", offset);
    }

    // One-off log report: where does the character manager refer to the new player or its character? It tells us
    // whether the player can be reached from the manager without scanning memory at all.
    void ProbeCharacterManager(const Dinki::Scan::ReadFn& read, const PlayerState& state) {
        const Results results = GetResults();
        if (!results.characterManager) {
            GogLog("GOG core: the character manager was not found, so there is no report on how it refers to the player");
            return;
        }
        const uintptr_t manager = results.characterManager;
        GogLog("GOG core: where does the character manager (0x%llX) refer to the player (0x%llX) or its character (0x%llX)?",
            static_cast<unsigned long long>(manager), static_cast<unsigned long long>(state.player),
            static_cast<unsigned long long>(state.character));

        int logged = 0;
        for (size_t offset = 0; offset + 8 <= 0x1000 && logged < 24; offset += 8) {
            uint64_t value = 0;
            if (!Dinki::Scan::Read64(read, manager + offset, value))
                continue;
            if (value == state.player || value == state.character) {
                GogLog("    manager+0x%03zX  holds the %s directly", offset, value == state.player ? "player" : "character");
                ++logged;
                continue;
            }
            if (value < 0x10000 || value >= 0x0000800000000000ull || (value & 7) != 0)
                continue;
            // a pointer: look inside what it points at (an array of pointers, or a list node)
            for (size_t inner = 0; inner + 8 <= 0x400 && logged < 24; inner += 8) {
                uint64_t entry = 0;
                if (!Dinki::Scan::Read64(read, static_cast<uintptr_t>(value) + inner, entry))
                    break;
                if (entry == state.player || entry == state.character) {
                    GogLog("    manager+0x%03zX -> +0x%03zX  holds the %s", offset, inner, entry == state.player ? "player" : "character");
                    ++logged;
                }
            }
        }
        if (!logged)
            GogLog("    (not found in the first 0x1000 bytes of the manager, looking two levels deep)");
    }

    void PlayerPass(const Dinki::Scan::ReadFn& read) {
        static ULONGLONG lastScan = 0;
        static int failedScans = 0;
        static std::vector<uint8_t> scratch(1u << 20);
        static uintptr_t lastAnalysed = 0;

        // Is the player we already have still there?
        const PlayerState current = GetPlayer();
        if (current.player) {
            const auto check = Dinki::Player::InspectPlayer(read, current.player, Dinki::Player::Layout(), kPlayerVftables, kCharacterVftables);
            if (check.Plausible() && check.character == current.character)
                return;
            GogLog("GOG core: the player object at 0x%llX is gone or has changed; looking again", static_cast<unsigned long long>(current.player));
            SetPlayer(PlayerState());
        }

        if (!Dinki::GetSettingBool(kPlayerScanSetting))
            return;                     // switched off in the menu or in dinki.ini
        if (!GameIsHealthy())
            return;                     // don't add load to a game that is already struggling (loading, cutscenes)

        // A heap scan is not free, so don't repeat it too often while there is no player (e.g. in the menus).
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG interval = failedScans < 15 ? 4000 : 30000;
        if (lastScan != 0 && now - lastScan < interval)
            return;
        lastScan = now;

        const ULONGLONG started = GetTickCount64();
        std::vector<Dinki::Scan::MemoryRegion> regions = EnumerateResidentRegions();
        const std::vector<uint64_t> values(kPlayerVftables.begin(), kPlayerVftables.end());
        // Don't let the scan find its own copies of the values it is looking for.
        auto exclude = [&regions](const void* data, size_t bytes) {
            regions = Dinki::Scan::SubtractRange(regions, reinterpret_cast<uintptr_t>(data), reinterpret_cast<uintptr_t>(data) + bytes);
        };
        exclude(scratch.data(), scratch.size());
        exclude(values.data(), values.size() * sizeof(uint64_t));
        exclude(kPlayerVftables.data(), kPlayerVftables.size() * sizeof(uintptr_t));

        unsigned long long totalBytes = 0;
        for (const auto& r : regions)
            totalBytes += r.end - r.begin;

        // Read the memory in slices with a short pause between them, and stop as soon as the player is found or
        // the game starts to struggle.
        const auto slices = Dinki::Scan::SliceRegions(regions, kScanSliceBytes);
        size_t hitCount = 0, plausible = 0;
        std::vector<uintptr_t> firstHits;
        uintptr_t bestAddress = 0;
        Dinki::Player::Check best;
        unsigned long long scanned = 0;
        bool stoppedEarly = false;

        for (const auto& slice : slices) {
            if (!Dinki::GetSettingBool(kPlayerScanSetting) || !GameIsHealthy()) {
                stoppedEarly = true;
                break;
            }
            ResetReadCache();
            const std::vector<uintptr_t> sliceHits = Dinki::Scan::FindQwordsInRegions(read, slice, values, scratch, 512);
            for (const auto& r : slice)
                scanned += r.end - r.begin;

            for (uintptr_t address : sliceHits) {
                ++hitCount;
                if (firstHits.size() < 6)
                    firstHits.push_back(address);
                const auto check = Dinki::Player::InspectPlayer(read, address, Dinki::Player::Layout(), kPlayerVftables, kCharacterVftables);
                if (!check.Plausible())
                    continue;
                ++plausible;
                if (!bestAddress || check.Score() > best.Score()) {
                    bestAddress = address;
                    best = check;
                }
            }
            if (bestAddress)
                break;                  // found it: no need to read the rest of the memory
            Sleep(3);                   // let the game have the memory bus between slices
        }

        const ULONGLONG elapsed = GetTickCount64() - started;
        GogLog("GOG core: player scan read %llu of %llu MB of resident memory in %llu ms%s: %zu vftable hit(s), %zu look like a player",
            scanned >> 20, totalBytes >> 20, static_cast<unsigned long long>(elapsed),
            stoppedEarly ? " (stopped early: the game slowed down, or the scan was switched off)" : "", hitCount, plausible);

        if (!bestAddress) {
            if (!stoppedEarly && ++failedScans <= 3) {
                GogLog("GOG core: no player found (attempt %d). The first hits and what was wrong with them:", failedScans);
                for (uintptr_t address : firstHits) {
                    const auto check = Dinki::Player::InspectPlayer(read, address, Dinki::Player::Layout(), kPlayerVftables, kCharacterVftables);
                    GogLog("    0x%llX  vftable %s  character-pointer %s (0x%llX)  self-pointer %s", static_cast<unsigned long long>(address),
                        check.vftableOk ? "ok" : "no", check.characterOk ? "ok" : "no", static_cast<unsigned long long>(check.character),
                        check.selfOk ? "ok" : "no");
                }
            }
            return;
        }

        failedScans = 0;
        PlayerState state;
        state.player = bestAddress;
        state.character = best.character;
        SetPlayer(state);
        GogLog("GOG core: PLAYER = 0x%llX, character = 0x%llX (self pointer %s%s)", static_cast<unsigned long long>(state.player),
            static_cast<unsigned long long>(state.character), best.selfOk ? "confirmed" : "NOT confirmed",
            plausible > 1 ? "; several candidates, the best-scoring one was chosen" : "");

        if (lastAnalysed != state.character) {
            lastAnalysed = state.character;
            AnalyseCharacter(read, state);
            ProbeCharacterManager(read, state);
        }
    }

    // ------------------------------------------------------------------------------------------
    // The vehicle (helper thread)
    // ------------------------------------------------------------------------------------------

    struct VehicleCandidate {
        uintptr_t base = 0;               // the start of the vehicle object
        std::string fields;               // which fields of the character point into it, e.g. "+0xB08 +0x1C40"
        int fieldCount = 0;
        uint64_t vftable = 0;             // what the object starts with
        float maxHealth = 0.0f, curHealth = 0.0f, lastHealth = 0.0f;
        bool haveHealth = false;
        bool adjusted = false;            // a pointer into the middle of the object was moved back to its start
        float distance = -1.0f;           // to the player; negative if unknown
    };

    struct VehicleState {
        uintptr_t selected = 0;           // the start of the vehicle to show
        float selectedDistance = -1.0f;
        std::vector<VehicleCandidate> candidates;
    };

    std::mutex g_vehicleMutex;
    VehicleState g_vehicle;

    const char* VehicleKind(uint64_t vftable) {
        for (size_t i = 0; i < 4; ++i)       // the first four entries of kVehicleVftables are CCar's
            if (kVehicleVftables[i] == vftable)
                return "CCar";
        return "CVehicle";
    }

    // Looks at every vehicle the player's character points at. Runs four times a second on the helper thread: it
    // reads a lot of memory, which the game thread must not do.
    //
    // The character can hold several pointers to the SAME car: one to the start of the object and others to
    // base-class parts of it a few bytes in (your log: +0xB08 and +0x1C40 were exactly 8 bytes apart). Health lives
    // at fixed offsets from the START, so a pointer into the middle has to be moved back before reading it.
    void VehiclePass(const Dinki::Scan::ReadFn& read) {
        static std::string lastSignature;
        static int logBlocks = 0;
        static int analyses = 0;
        static uintptr_t lastAnalysed = 0;
        static uint64_t primary = kVehicleVftables[0];                       // what the start of a vehicle holds
        static size_t positionOffset = static_cast<size_t>(-1);              // where a vehicle keeps its world position, once found

        VehicleState next;
        const PlayerState player = GetPlayer();
        if (player.character) {
            float px = 0, py = 0, pz = 0;
            const bool havePlayerPosition = SafeReadValue(player.character + 0x208, px) &&
                SafeReadValue(player.character + 0x20C, py) && SafeReadValue(player.character + 0x210, pz);

            std::vector<Dinki::Player::Vehicle::FieldPointer> raw;
            for (size_t offset : Dinki::Scan::FindPointerFields(read, player.character, kCharacterScanBytes, IsVehicleVftable)) {
                uint64_t pointer = 0;
                if (SafeReadValue(player.character + offset, pointer))
                    raw.push_back({ offset, static_cast<uintptr_t>(pointer) });
            }

            // Which vftable does the START of a vehicle hold? Two pointers into the same object tell us.
            const uint64_t learned = Dinki::Player::Vehicle::LearnStartVftable(read, raw, kVehicleVftables, primary, 0x40);
            if (learned != primary) {
                primary = learned;
                GogLog("GOG core: the start of a vehicle object holds the vftable 0x%llX (learned from two pointers into the same object)",
                    static_cast<unsigned long long>(primary));
            }

            // Move every pointer back to the start of its object and merge the ones that reach the same vehicle.
            for (const auto& group : Dinki::Player::Vehicle::GroupPointers(read, raw, primary, 0x40)) {
                VehicleCandidate c;
                c.base = group.base;
                for (size_t field : group.fields)
                    c.fields += (c.fields.empty() ? "" : " ") + Fmt("+0x%zX", field);
                c.fieldCount = static_cast<int>(group.fields.size());
                c.adjusted = group.adjusted;
                SafeReadValue(c.base, c.vftable);
                c.haveHealth = SafeReadValue(c.base + 0x180, c.maxHealth) && SafeReadValue(c.base + 0x184, c.curHealth) &&
                    SafeReadValue(c.base + 0x188, c.lastHealth);
                next.candidates.push_back(c);
            }

            // How far is each from the player? A vehicle keeps its world position somewhere other than a character
            // does, so find where: the float triple closest to the player's position.
            if (havePlayerPosition) {
                for (VehicleCandidate& c : next.candidates) {
                    if (positionOffset != static_cast<size_t>(-1)) {
                        float x = 0, y = 0, z = 0;
                        if (SafeReadValue(c.base + positionOffset, x) && SafeReadValue(c.base + positionOffset + 4, y) &&
                            SafeReadValue(c.base + positionOffset + 8, z))
                            c.distance = std::sqrt((x - px) * (x - px) + (y - py) * (y - py) + (z - pz) * (z - pz));
                        continue;
                    }
                    float bestDistance = -1.0f;
                    size_t bestOffset = static_cast<size_t>(-1);
                    for (const auto& t : Dinki::Player::FindPositionLikeTriples(read, c.base, 0x1000, 400)) {
                        const float d = std::sqrt((t.x - px) * (t.x - px) + (t.y - py) * (t.y - py) + (t.z - pz) * (t.z - pz));
                        if (bestDistance < 0.0f || d < bestDistance) {
                            bestDistance = d;
                            bestOffset = t.offset;
                        }
                    }
                    c.distance = bestDistance;
                    if (bestOffset != static_cast<size_t>(-1) && bestDistance <= 30.0f) {
                        positionOffset = bestOffset;
                        GogLog("GOG core: a vehicle keeps its world position at +0x%zX (%.1f from the player when found)", bestOffset, bestDistance);
                    }
                }
            }

            // Show the vehicle the character points at most often (the one it is in, over one it is only near).
            const VehicleCandidate* best = nullptr;
            for (const VehicleCandidate& c : next.candidates)
                if (!best || c.fieldCount > best->fieldCount)
                    best = &c;
            if (best) {
                next.selected = best->base;
                next.selectedDistance = best->distance;
            }
        }

        // Log when the vehicles (or the choice) change.
        std::string signature;
        for (const VehicleCandidate& c : next.candidates)
            signature += Fmt("%llX:%s;", static_cast<unsigned long long>(c.base), c.fields.c_str());
        signature += Fmt("=>%llX", static_cast<unsigned long long>(next.selected));
        if (signature != lastSignature) {
            lastSignature = signature;
            if (logBlocks++ < 60) {
                if (next.candidates.empty()) {
                    GogLog("GOG core: the character no longer points at any vehicle");
                }
                else {
                    GogLog("GOG core: the character points at %zu vehicle(s):", next.candidates.size());
                    for (const VehicleCandidate& c : next.candidates) {
                        const std::string health = c.haveHealth ? Fmt("%.1f / %.1f / %.1f", c.curHealth, c.maxHealth, c.lastHealth) : std::string("?");
                        const std::string distance = c.distance >= 0.0f ? Fmt("%.1f", c.distance) : std::string("?");
                        GogLog("    vehicle 0x%llX (%s, starts with 0x%llX)  via character %s  health cur/max/last %s  distance to player %s%s%s",
                            static_cast<unsigned long long>(c.base), VehicleKind(c.vftable), static_cast<unsigned long long>(c.vftable),
                            c.fields.c_str(), health.c_str(), distance.c_str(),
                            c.adjusted ? "  [a pointer was moved back to the start of the object]" : "",
                            c.base == next.selected ? "   <== shown" : "");
                    }
                }
            }
        }

        // A newly shown vehicle gets a one-off report, to check the health fields from the log.
        if (next.selected && next.selected != lastAnalysed && analyses < 6) {
            lastAnalysed = next.selected;
            ++analyses;
            GogLog("GOG core: report on the vehicle 0x%llX", static_cast<unsigned long long>(next.selected));
            HexDump("    its first 0x30 bytes", next.selected, 0x30);
            const auto pairs = Dinki::Player::FindHealthLikePairs(read, next.selected, 0x1000, 10);
            GogLog("    health-like (max, current) pairs in its first 0x1000 bytes (up to 10):");
            for (const auto& p : pairs)
                GogLog("        +0x%03zX   max %.3f   current %.3f", p.offset, p.first, p.second);
        }

        std::lock_guard<std::mutex> lock(g_vehicleMutex);
        g_vehicle = next;
    }

    DWORD WINAPI BootstrapThread(LPVOID) {
        GogLog("GOG core: helper thread started; waiting a moment for the game to start up");
        Sleep(3000);

        bool presentHooked = false;
        for (int attempt = 1; attempt <= 10 && !presentHooked; ++attempt) {
            presentHooked = Dinki::InstallPresentHook();
            if (!presentHooked) {
                GogLog("GOG core: Present hook attempt %d failed; retrying in 2 seconds", attempt);
                Sleep(2000);
            }
        }
        if (!presentHooked)
            GogLog("GOG core: GIVING UP on the Present hook, so there will be no overlay");

        // Scans must not compete with the game for CPU.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        const Dinki::Scan::ReadFn read = [](uintptr_t address, void* out, size_t size) { return SafeRead(address, out, size); };

        // Objects are created while the game loads, so keep looking until everything has been found, and keep
        // checking that the player is still the player (it is recreated when you reload).
        bool discoveryDone = false;
        for (int pass = 1;; ++pass) {
            ResetReadCache();
            if (!discoveryDone) {
                discoveryDone = DiscoveryPass(pass);
                if (discoveryDone)
                    GogLog("GOG core: object discovery complete");
                else if (pass % 30 == 0)
                    GogLog("GOG core: still looking after %d passes (tick %llu, frames %llu, game window %s)", pass,
                        g_tickCount.load(), Dinki::PresentCount(), Dinki::PresentWindow() ? "known" : "not yet seen");
            }

            PlayerPass(read);

            // what our own code costs the game (the first three minutes, every 10 seconds)
            if (pass % 10 == 0 && pass <= 180) {
                GogLog("GOG core: perf  tick %u/s (our hook: avg %.2f ms, max %.2f ms = watches %.2f + commands and renderers %.2f)   frame %u/s (overlay: avg %.2f ms, max %.2f ms)",
                    g_ticksPerSecond.load(), g_tickCostAvgUs.load() / 1000.0, g_tickCostMaxUs.load() / 1000.0,
                    g_watchCostAvgUs.load() / 1000.0, g_processCostAvgUs.load() / 1000.0,
                    g_framesPerSecond.load(), g_overlayCostAvgUs.load() / 1000.0, g_overlayCostMaxUs.load() / 1000.0);

                const Dinki::TickStats commands = Dinki::TakeTickStats();
                GogLog("GOG core: perf  command polling: %u calls, avg %.3f ms, max %.3f ms", commands.calls, commands.avgMs, commands.maxMs);

                std::vector<Dinki::WatchCost> costs = Dinki::TakeWatchCosts();
                std::sort(costs.begin(), costs.end(), [](const Dinki::WatchCost& a, const Dinki::WatchCost& b) { return a.avgMs > b.avgMs; });
                std::string slowest;
                for (size_t i = 0; i < costs.size() && i < 6; ++i)
                    slowest += Fmt("  %s %.3f", costs[i].id.c_str(), costs[i].avgMs);
                GogLog("GOG core: perf  watch cost, avg ms each (slowest first):%s", slowest.empty() ? "  (none sampled)" : slowest.c_str());
            }

            if (pass == 15)
                GogLog("GOG core: threads  tick=%u  present=%u  (%s)", g_tickThread.load(), Dinki::PresentThreadId(),
                    g_tickThread.load() == Dinki::PresentThreadId() ? "the same thread" : "different threads");
            // The vehicle lookup runs four times a second, here and never on the game thread.
            for (int i = 0; i < 4; ++i) {
                Sleep(250);
                ResetReadCache();
                VehiclePass(read);
            }
        }
    }

    // ------------------------------------------------------------------------------------------
    // Input grab
    // ------------------------------------------------------------------------------------------

    void WriteFocusFlag(bool inFocus) {
        const Results results = GetResults();
        if (!results.inputManager)
            return;
        // Same effect as CInputDeviceManager::SetInFocus(bool): a single byte.
        volatile bool* flag = reinterpret_cast<volatile bool*>(results.inputManager + kInputFocusOffset);
        uint8_t current = 0;
        if (FastReadValue(results.inputManager + kInputFocusOffset, current) && current <= 1)
            *flag = inFocus;
    }

} // namespace

// ----------------------------------------------------------------------------------------------
// Watches shown on the overlay so you can see the core working
// ----------------------------------------------------------------------------------------------

static const int kWatchCore = Dinki::RegisterWatch("gog.core", "GOG core status", "DINKI (GOG core)", 0x40E0D0, true,
    [](void*, std::string& text) {
        if (!Dinki::IsGog())
            return Dinki::WatchStatus::Hidden;
        text = Fmt("tick:  %u/s  thread %u  (our cost %.2f ms, max %.2f)\nframe: %u/s  thread %u  (overlay %.2f ms, max %.2f)",
            g_ticksPerSecond.load(), g_tickThread.load(), g_tickCostAvgUs.load() / 1000.0, g_tickCostMaxUs.load() / 1000.0,
            g_framesPerSecond.load(), Dinki::PresentThreadId(), g_overlayCostAvgUs.load() / 1000.0, g_overlayCostMaxUs.load() / 1000.0);
        return Dinki::WatchStatus::Ok;
    });

static const int kWatchMissions = Dinki::RegisterWatch("gog.missions", "Mission manager", "MISSIONS", 0xFFD060, true,
    [](void*, std::string& text) {
        if (!Dinki::IsGog())
            return Dinki::WatchStatus::Hidden;
        const Results results = GetResults();
        if (!results.missionManager) {
            text = "searching for the manager...";
            return Dinki::WatchStatus::Ok;
        }
        text = Fmt("manager 0x%llX%s", static_cast<unsigned long long>(results.missionManager),
            results.missionManagerConfirmed ? "" : " (unconfirmed)");
        if (!results.haveMissionVector) {
            text += "\nmission list: searching...";
            return Dinki::WatchStatus::Ok;
        }
        uint64_t first = 0, last = 0;
        if (!FastReadValue(results.missionManager + results.missionVectorOffset, first) ||
            !FastReadValue(results.missionManager + results.missionVectorOffset + 8, last))
            return Dinki::WatchStatus::Failed;
        text += Fmt("\nmissions: %llu", static_cast<unsigned long long>((last - first) / 8));
        return Dinki::WatchStatus::Ok;
    });

static const int kWatchVehicleCandidates = Dinki::RegisterWatch("gog.vehicle_candidates", "Vehicles the character points at (debugging)", "VEHICLES SEEN", 0xFF80FF, false,
    [](void*, std::string& text) {
        if (!Dinki::IsGog())
            return Dinki::WatchStatus::Hidden;
        text = Dinki::GogVehicleCandidatesText();
        return text.empty() ? Dinki::WatchStatus::Hidden : Dinki::WatchStatus::Ok;
    });

static const int kWatchInput = Dinki::RegisterWatch("gog.input", "Game input focus flag", "INPUT", 0xA0A0FF, false,
    [](void*, std::string& text) {
        if (!Dinki::IsGog())
            return Dinki::WatchStatus::Hidden;
        const Results results = GetResults();
        if (!results.inputManager) {
            text = "input manager not confirmed";
            return Dinki::WatchStatus::Ok;
        }
        uint8_t flag = 0;
        if (!FastReadValue(results.inputManager + kInputFocusOffset, flag))
            return Dinki::WatchStatus::Failed;
        text = Fmt("in focus: %u   (menu %s)", static_cast<unsigned>(flag), Dinki::IsInputBlocked() ? "open" : "closed");
        return Dinki::WatchStatus::Ok;
    });

// ----------------------------------------------------------------------------------------------
// Public entry points
// ----------------------------------------------------------------------------------------------

void Dinki::GogStartup() {
    GogLog("=== Dinki GOG core: this exe is a recognized GOG build; the legacy address tables are NOT used ===");

    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        GogLog("GOG core: MH_Initialize failed (MinHook status %d)", static_cast<int>(init));

    InstallTickHook();

    // Hooking Present means creating a Direct3D device, which must not happen inside DllMain.
    HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    else
        GogLog("GOG core: could not start the helper thread (error %lu)", GetLastError());
}

uintptr_t Dinki::GogPlayerCharacter() {
    return GetPlayer().character;
}

bool Dinki::GogPlayerVehicleInfo(uintptr_t& base, float& distanceToPlayer) {
    std::lock_guard<std::mutex> lock(g_vehicleMutex);
    if (!g_vehicle.selected)
        return false;
    base = g_vehicle.selected;
    distanceToPlayer = g_vehicle.selectedDistance;
    return true;
}

uintptr_t Dinki::GogPlayerVehicle() {
    uintptr_t base = 0;
    float distance = 0.0f;
    return GogPlayerVehicleInfo(base, distance) ? base : 0;
}

std::string Dinki::GogVehicleCandidatesText() {
    VehicleState copy;
    {
        std::lock_guard<std::mutex> lock(g_vehicleMutex);
        copy = g_vehicle;
    }
    std::string text;
    for (const VehicleCandidate& c : copy.candidates) {
        float maxHealth = 0.0f, curHealth = 0.0f;   // re-read now, so the values are live
        const bool ok = FastReadValue(c.base + 0x180, maxHealth) && FastReadValue(c.base + 0x184, curHealth);
        const std::string health = ok ? Fmt("%.1f/%.1f", curHealth, maxHealth) : std::string("?");
        if (!text.empty())
            text += "\n";
        text += Fmt("[%s] %s  hp %s%s", c.fields.c_str(), VehicleKind(c.vftable), health.c_str(), c.base == copy.selected ? "  <" : "");
    }
    return text;
}

void Dinki::GogSetInputGrab(bool grab) {
    Dinki::SetInputBlocked(grab);
    WriteFocusFlag(!grab);
    if (grab)
        ClipCursor(nullptr);   // the game may have confined the cursor to its window; let the menu use it
}
