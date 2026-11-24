#include "dinki_diag.h"
#include "dinki_build.h"
#include "plugin.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

    // Same values the ACT_EXE macro uses to decide "this is the GOG build".
    constexpr uintptr_t kFingerprintAddress = 0x14192A150;
    constexpr uint64_t  kFingerprintGog     = 0x1c3c00001bf0;
    constexpr uintptr_t kSteamStartupAddress = 0x14303978D;   // _tmainCRTStartup on the Steam build

    bool ReadBytes(uintptr_t address, void* out, size_t size) {
        SIZE_T got = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &got) && got == size;
    }

    std::string HexBytes(uintptr_t address, size_t count) {
        unsigned char bytes[32] = {};
        if (count > sizeof(bytes))
            count = sizeof(bytes);
        if (!ReadBytes(address, bytes, count))
            return "(unreadable)";
        std::string text;
        char piece[4];
        for (size_t i = 0; i < count; ++i) {
            std::snprintf(piece, sizeof(piece), "%02X ", bytes[i]);
            text += piece;
        }
        return text;
    }

    struct Site {
        const char* name;
        uintptr_t gog;
        uintptr_t steam;
    };

    // Every code address Dinki patches or hooks, as (GOG column, Steam column).
    const Site kSites[] = {
        { "CPlayer::UpdateController", 0x1404C6670, 0x1420DEAC0 },
        { "CVideoManager::Update",     0x1405DE290, 0x142215D90 },
        { "UpdatePostSim",             0x1403B0840, 0x141FA5A10 },
        { "DX11 render hook 1",        0x140F17A90, 0x142F5EDF0 },
        { "DX11 render hook 2",        0x1408788E0, 0x142562700 },
        { "DX11 render hook 3",        0x1408790E0, 0x142562F10 },
        { "spawn constructor",         0x1402E19D0, 0x141EB2410 },
        { "spawn destructor",          0x1402E2320, 0x141EB2E10 },
        { "vehicle spawn limit patch", 0x1403DB400, 0x141FD4D0C },
        { "character spawn limit patch", 0x1403DB402, 0x141FD4D0E },
        { "vehicle lock NOP patch",    0x1405CD1C5, 0x142203AEB },
    };

    void LogSite(const char* column, uintptr_t address, uintptr_t imageBase, uintptr_t imageEnd) {
        const bool inImage = address >= imageBase && address < imageEnd;
        Log("    %-5s 0x%llX  %s  bytes: %s", column, static_cast<unsigned long long>(address),
            inImage ? "in image    " : "OUTSIDE image", HexBytes(address, 16).c_str());
    }

} // namespace

void Dinki::LogBuildDiagnostics() {
    Log("=== Dinki build diagnostics ===");

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    Log("exe: %s", exePath);

    WIN32_FILE_ATTRIBUTE_DATA fileInfo = {};
    if (GetFileAttributesExA(exePath, GetFileExInfoStandard, &fileInfo)) {
        const unsigned long long size = (static_cast<unsigned long long>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
        Log("file size: %llu bytes", size);
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("could not read the exe's PE header");
        return;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        Log("unexpected PE signature");
        return;
    }

    const uintptr_t imageEnd = base + nt->OptionalHeader.SizeOfImage;
    Log("image base: 0x%llX   size of image: 0x%X   entry point RVA: 0x%X",
        static_cast<unsigned long long>(base), nt->OptionalHeader.SizeOfImage, nt->OptionalHeader.AddressOfEntryPoint);
    Log("PE timestamp: 0x%08X   checksum: 0x%08X   sections: %u",
        nt->FileHeader.TimeDateStamp, nt->OptionalHeader.CheckSum, static_cast<unsigned>(nt->FileHeader.NumberOfSections));

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9] = {};
        std::memcpy(name, section->Name, 8);
        Log("    section %-8s  VA 0x%08X  size 0x%08X  flags 0x%08X", name,
            section->VirtualAddress, section->Misc.VirtualSize, section->Characteristics);
    }

    // ---- build detection, exactly as the ACT_EXE macro does it ----
    uint64_t fingerprint = 0;
    const bool readable = ReadBytes(kFingerprintAddress, &fingerprint, sizeof(fingerprint));
    if (readable)
        Log("fingerprint at 0x%llX: 0x%llX (GOG build expects 0x%llX)", static_cast<unsigned long long>(kFingerprintAddress),
            static_cast<unsigned long long>(fingerprint), static_cast<unsigned long long>(kFingerprintGog));
    else
        Log("fingerprint at 0x%llX: UNREADABLE (outside this exe's image?)", static_cast<unsigned long long>(kFingerprintAddress));

    const bool detectedGog = readable && fingerprint == kFingerprintGog;
    Log("Dinki will treat this exe as: %s", detectedGog ? "GOG (uses the first address column)"
                                                       : "STEAM (uses the second address column; hooks wait for the Steam startup address)");
    Log("Dinki build classification: %s", Dinki::IsGog() ? "recognized GOG build -> GOG core (no address tables)" : "legacy -> original address tables");
    Log("Steam startup address 0x%llX: %s", static_cast<unsigned long long>(kSteamStartupAddress),
        (kSteamStartupAddress >= base && kSteamStartupAddress < imageEnd) ? "inside this image" : "OUTSIDE this image");

    // ---- do the addresses in each column point somewhere real? ----
    int gogInImage = 0, steamInImage = 0;
    const int total = static_cast<int>(sizeof(kSites) / sizeof(kSites[0]));
    for (const Site& site : kSites) {
        Log("  %s", site.name);
        LogSite("GOG", site.gog, base, imageEnd);
        LogSite("Steam", site.steam, base, imageEnd);
        if (site.gog >= base && site.gog < imageEnd) ++gogInImage;
        if (site.steam >= base && site.steam < imageEnd) ++steamInImage;
    }
    Log("addresses inside this exe's image: GOG column %d/%d, Steam column %d/%d", gogInImage, total, steamInImage, total);
    Log("=== end of diagnostics ===");
}
