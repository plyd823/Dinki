#pragma once

// Which build of Mad Max is this? Decided from the exe's PE header, so it never reads game memory
// at a hard-coded address.
//
//   Gog2015  the GOG exe built 2015-12-01 (25,735,680 bytes). Uses the GOG core in dinki_gog.cpp,
//            which finds what it needs at runtime instead of using a table of addresses.
//   Legacy   anything else. Uses the original address tables (the Steam build, and the GOG build
//            those tables were written for).

#include <cstdint>

namespace Dinki {

    enum class Build {
        Legacy,
        Gog2015,
    };

    constexpr uint32_t kGog2015Timestamp   = 0x565D5965;   // IMAGE_FILE_HEADER.TimeDateStamp
    constexpr uint32_t kGog2015SizeOfImage = 0x01AAE000;   // IMAGE_OPTIONAL_HEADER.SizeOfImage

    // Pure: decides from the two header values.
    Build ClassifyBuild(uint32_t timestamp, uint32_t sizeOfImage);

    // Reads the running exe's header (once) and classifies it. Returns Legacy if it can't be read.
    Build GetBuild();

    inline bool IsGog() { return GetBuild() == Build::Gog2015; }

} // namespace Dinki
