#pragma once

// Memory scanning helpers used to find game objects at runtime, so no instance address has to be
// hard-coded. Everything reads memory through a ReadFn, which makes the logic testable without a game.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace Dinki {
namespace Scan {

    // Copies `size` bytes from `address` into `out`. Returns false if the memory can't be read.
    using ReadFn = std::function<bool(uintptr_t address, void* out, size_t size)>;

    struct Range {
        uintptr_t begin = 0;
        uintptr_t end = 0;      // one past the last byte
        bool Contains(uintptr_t a) const { return a >= begin && a < end; }
    };

    bool Read64(const ReadFn& read, uintptr_t address, uint64_t& out);

    struct ObjectHit {
        uintptr_t base = 0;            // start of the object that contains the vftable pointer
        size_t vftableOffset = 0;      // where in that object the vftable pointer was found
        bool isStatic = false;         // the object itself lives inside the scanned range
        uintptr_t slot = 0;            // address in the scanned range that points at it (0 if static)
        int refs = 1;                  // how many slots in the range point at the same object
    };

    // Finds objects whose vftable pointer equals `vftable`.
    //
    //  * Heap objects: `data` is scanned for pointers; each plausible pointer p is followed and counts
    //    as a hit if the 8 bytes at p + offset equal the vftable, for any offset in `offsets`
    //    (use several to find a class embedded inside a larger object).
    //  * Static objects: if a slot in `data` itself holds the vftable, that slot is a hit (isStatic).
    //
    // Pointers into `image` are skipped (they point at code or constants, not objects).
    std::vector<ObjectHit> FindObjects(const ReadFn& read, Range data, Range image, uintptr_t vftable,
        const std::vector<size_t>& offsets);

    struct MultiHit {
        uintptr_t vftable = 0;     // which of the vftables was matched
        ObjectHit hit;
    };

    // Same as FindObjects for many vftables at once, in a single pass over `data`. Each heap pointer is
    // followed once per offset no matter how many vftables are being looked for, and one object can
    // produce several hits (a class with multiple inheritance has a vftable pointer for each base).
    std::vector<MultiHit> FindObjectsMulti(const ReadFn& read, Range data, Range image,
        const std::vector<uintptr_t>& vftables, const std::vector<size_t>& offsets);

    struct VectorHit {
        size_t offset = 0;         // offset of the vector inside the object
        uintptr_t begin = 0;       // address of the first element
        size_t count = 0;          // number of elements
    };

    // Called with the first 8 bytes of an element object (its vftable pointer).
    using VftablePredicate = std::function<bool(uint64_t vftable)>;

    // Looks inside `object` (the first `scanBytes` bytes) for something shaped like an MSVC
    // std::vector<T*>: three consecutive pointers (first, last, end-of-capacity) where every element
    // is a pointer to an object whose vftable satisfies `isElementVftable`.
    // Results are sorted by element count, largest first.
    std::vector<VectorHit> FindPointerVectors(const ReadFn& read, uintptr_t object, size_t scanBytes,
        const VftablePredicate& isElementVftable);

    // ---- scanning whole regions of memory (for objects nothing in .data points at) ----

    struct MemoryRegion {
        uintptr_t begin = 0;
        uintptr_t end = 0;      // one past the last byte
    };

    // Returns `regions` with [begin, end) cut out of them. Used to keep a scan from reading its own buffer.
    std::vector<MemoryRegion> SubtractRange(const std::vector<MemoryRegion>& regions, uintptr_t begin, uintptr_t end);

    // Finds every 8-byte-aligned address in `regions` whose 8 bytes equal one of `values`, reading `scratch.size()`
    // bytes at a time (scratch's size is rounded down to a multiple of 8). Unreadable chunks are skipped. Stops
    // after maxHits. The caller owns `scratch` so it can exclude that buffer from `regions`.
    std::vector<uintptr_t> FindQwordsInRegions(const ReadFn& read, const std::vector<MemoryRegion>& regions,
        const std::vector<uint64_t>& values, std::vector<uint8_t>& scratch, size_t maxHits);

    // Asks which pages are resident (already in the process's working set). `resident` has one entry per page,
    // starting at `firstPage`, all false on entry.
    using ResidencyFn = std::function<void(uintptr_t firstPage, size_t pageCount, std::vector<bool>& resident)>;

    // Returns only the parts of `regions` made of resident pages, as runs of consecutive pages (clipped to the
    // original regions). Reading a page that was never touched makes Windows allocate and zero real memory
    // for it, which a scan must not do; when the residency can't be queried this returns nothing.
    std::vector<MemoryRegion> KeepResidentPages(const std::vector<MemoryRegion>& regions, size_t pageSize,
        size_t batchPages, const ResidencyFn& query);

    // Cuts `regions` into slices of at most maxBytes each (splitting regions where needed), in order, so a long
    // scan can pause between slices instead of hogging memory bandwidth.
    std::vector<std::vector<MemoryRegion>> SliceRegions(const std::vector<MemoryRegion>& regions, size_t maxBytes);

    // Offsets (multiples of 8, within the first scanBytes of `object`) of fields that hold a pointer to an
    // object whose first 8 bytes (its vftable) satisfy `isTargetVftable`.
    std::vector<size_t> FindPointerFields(const ReadFn& read, uintptr_t object, size_t scanBytes,
        const VftablePredicate& isTargetVftable);

} // namespace Scan
} // namespace Dinki
