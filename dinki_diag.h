#pragma once

namespace Dinki {

    // Writes what Dinki can see about the running exe (version, sections, which build it thinks this is,
    // and whether each hook address looks valid) to mm_log.txt. Read-only: it never patches anything.
    // Call it before HookMgr::Initialize() so the bytes it prints are the original, unhooked code.
    void LogBuildDiagnostics();

} // namespace Dinki
