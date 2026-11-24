#pragma once

// Recognizing the player in memory, and sanity-checking fields of the player's character. Pure logic over a
// ReadFn, so it is testable without a game (see dinki_scan.h).

#include "dinki_scan.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Dinki {
namespace Player {

    // Where CPlayer keeps its links (from Dinki's CPlayer struct).
    struct Layout {
        size_t characterOffset = 0x10;   // CPlayer::m_character, a pointer to the CCharacter it controls
        size_t selfOffset = 0x18;        // CPlayer::m_This, a pointer back at the CPlayer itself
    };

    struct Check {
        bool vftableOk = false;          // the object starts with one of CPlayer's vftables
        bool characterOk = false;        // m_character points at an object that starts with a CCharacter vftable
        bool selfOk = false;             // m_This points back at this object
        uintptr_t character = 0;         // the m_character pointer (0 if it couldn't be read)

        // Enough to trust it: the right class AND a character pointer that leads to the right class.
        bool Plausible() const { return vftableOk && characterOk; }
        int Score() const { return (vftableOk ? 1 : 0) + (characterOk ? 2 : 0) + (selfOk ? 1 : 0); }
    };

    Check InspectPlayer(const Scan::ReadFn& read, uintptr_t address, const Layout& layout,
        const std::vector<uintptr_t>& playerVftables, const std::vector<uintptr_t>& characterVftables);

    // ---- heuristics for finding fields in an object we don't have a layout for ----

    struct PairHit {
        size_t offset = 0;
        float first = 0.0f;
        float second = 0.0f;
    };

    struct TripleHit {
        size_t offset = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // Two consecutive floats shaped like (maximum health, current health): 1 <= max <= 100000 and 0 < current <= max.
    // Pairs whose maximum is a whole number come first (health is almost always a whole number).
    std::vector<PairHit> FindHealthLikePairs(const Scan::ReadFn& read, uintptr_t base, size_t bytes, size_t maxResults);

    // Three consecutive floats shaped like a world position: each finite and under 100000, at least two of them
    // at least 1 away from zero (which also rules out unit vectors and rotation-matrix rows), and not all equal.
    std::vector<TripleHit> FindPositionLikeTriples(const Scan::ReadFn& read, uintptr_t base, size_t bytes, size_t maxResults);

    // ---- vehicles ----
    //
    // The player's character can hold several pointers to the SAME car: one to the start of the object and others to
    // base-class parts of it a few bytes in (on the GOG build, character+0xB08 and character+0x1C40 were exactly
    // 8 bytes apart). Fields such as health sit at fixed offsets from the START, so a pointer into the middle of the
    // object has to be moved back to the start before anything is read from it.
    namespace Vehicle {

        struct FieldPointer {
            size_t field = 0;            // offset of the pointer in the character
            uintptr_t pointer = 0;       // what it points at
        };

        struct Group {
            uintptr_t base = 0;              // the start of the vehicle object
            std::vector<size_t> fields;      // every field of the character that points into it
            bool adjusted = false;           // at least one pointer had to be moved back to reach the start
        };

        // Two pointers within maxGap bytes of each other point into the same object, and the lower one is its start.
        // Returns the vftable held there if it is one of `vehicleVftables`, otherwise `current`.
        uint64_t LearnStartVftable(const Scan::ReadFn& read, const std::vector<FieldPointer>& pointers,
            const std::vector<uintptr_t>& vehicleVftables, uint64_t current, size_t maxGap);

        // Moves `pointer` back, 8 bytes at a time and at most maxBack, to an address that holds `startVftable`.
        // Returns the pointer unchanged (adjusted = false) if it already holds it, or if no such address is found.
        uintptr_t ResolveStart(const Scan::ReadFn& read, uintptr_t pointer, uint64_t startVftable, size_t maxBack,
            bool& adjusted);

        // Resolves every pointer and merges the ones that reach the same object.
        std::vector<Group> GroupPointers(const Scan::ReadFn& read, const std::vector<FieldPointer>& pointers,
            uint64_t startVftable, size_t maxBack);

    } // namespace Vehicle

} // namespace Player
} // namespace Dinki
