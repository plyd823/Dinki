#include "dinki_build.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Dinki {

Build ClassifyBuild(uint32_t timestamp, uint32_t sizeOfImage) {
    // Both must match: the timestamp alone isn't unique if a build is repacked.
    if (timestamp == kGog2015Timestamp && sizeOfImage == kGog2015SizeOfImage)
        return Build::Gog2015;
    return Build::Legacy;
}

Build GetBuild() {
#ifdef _WIN32
    static const Build build = []() {
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!base)
            return Build::Legacy;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return Build::Legacy;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return Build::Legacy;
        return ClassifyBuild(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    }();
    return build;
#else
    return Build::Legacy;
#endif
}

} // namespace Dinki
