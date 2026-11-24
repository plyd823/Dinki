#include "dinki_scan.h"

#include <algorithm>
#include <cstring>

namespace Dinki {
namespace Scan {

namespace {

    constexpr uintptr_t kMinPointer = 0x10000;
    constexpr uintptr_t kMaxPointer = 0x0000800000000000ull;   // top of the user-mode address space
    constexpr size_t kMaxVectorElements = 20000;

    // Could this value be a pointer to a heap object we should look at?
    bool PlausibleObjectPointer(uint64_t value, Range image) {
        return value >= kMinPointer && value < kMaxPointer && (value & 7) == 0 && !image.Contains(static_cast<uintptr_t>(value));
    }

} // namespace

bool Read64(const ReadFn& read, uintptr_t address, uint64_t& out) {
    return read && read(address, &out, sizeof(out));
}

std::vector<MultiHit> FindObjectsMulti(const ReadFn& read, Range data, Range image,
    const std::vector<uintptr_t>& vftables, const std::vector<size_t>& offsets)
{
    std::vector<MultiHit> hits;
    if (!read || vftables.empty())
        return hits;

    auto isWanted = [&vftables](uint64_t value) {
        for (uintptr_t v : vftables)
            if (v == value)
                return true;
        return false;
    };

    auto addHit = [&hits](uintptr_t vftable, uintptr_t base, size_t offset, bool isStatic, uintptr_t slot) {
        for (MultiHit& h : hits) {
            if (h.vftable == vftable && h.hit.base == base && h.hit.vftableOffset == offset) {
                ++h.hit.refs;
                return;
            }
        }
        MultiHit entry;
        entry.vftable = vftable;
        entry.hit.base = base;
        entry.hit.vftableOffset = offset;
        entry.hit.isStatic = isStatic;
        entry.hit.slot = slot;
        hits.push_back(entry);
    };

    for (uintptr_t slot = data.begin; slot + sizeof(uint64_t) <= data.end; slot += sizeof(uint64_t)) {
        uint64_t value = 0;
        if (!Read64(read, slot, value))
            continue;

        if (isWanted(value)) {
            addHit(static_cast<uintptr_t>(value), slot, 0, true, 0);   // an object embedded in the scanned range
            continue;
        }

        if (!PlausibleObjectPointer(value, image))
            continue;

        for (size_t offset : offsets) {
            uint64_t first = 0;
            if (Read64(read, static_cast<uintptr_t>(value) + offset, first) && isWanted(first))
                addHit(static_cast<uintptr_t>(first), static_cast<uintptr_t>(value), offset, false, slot);
        }
    }
    return hits;
}

std::vector<ObjectHit> FindObjects(const ReadFn& read, Range data, Range image, uintptr_t vftable,
    const std::vector<size_t>& offsets)
{
    std::vector<ObjectHit> hits;
    for (const MultiHit& m : FindObjectsMulti(read, data, image, { vftable }, offsets))
        hits.push_back(m.hit);
    return hits;
}

std::vector<VectorHit> FindPointerVectors(const ReadFn& read, uintptr_t object, size_t scanBytes,
    const VftablePredicate& isElementVftable)
{
    std::vector<VectorHit> hits;
    if (!isElementVftable)
        return hits;

    for (size_t offset = 0; offset + 3 * sizeof(uint64_t) <= scanBytes; offset += sizeof(uint64_t)) {
        uint64_t first = 0, last = 0, end = 0;
        if (!Read64(read, object + offset, first) ||
            !Read64(read, object + offset + 8, last) ||
            !Read64(read, object + offset + 16, end))
            continue;

        if (first < kMinPointer || first > last || last > end || ((last - first) & 7) != 0)
            continue;

        const size_t count = static_cast<size_t>((last - first) / sizeof(uint64_t));
        if (count == 0 || count > kMaxVectorElements)
            continue;

        // Every element must be a pointer to an object whose vftable we accept.
        bool allMatch = true;
        for (size_t i = 0; i < count && allMatch; ++i) {
            uint64_t element = 0, vftable = 0;
            allMatch = Read64(read, static_cast<uintptr_t>(first) + i * sizeof(uint64_t), element) &&
                element >= kMinPointer && element < kMaxPointer &&
                Read64(read, static_cast<uintptr_t>(element), vftable) &&
                isElementVftable(vftable);
        }
        if (!allMatch)
            continue;

        VectorHit hit;
        hit.offset = offset;
        hit.begin = static_cast<uintptr_t>(first);
        hit.count = count;
        hits.push_back(hit);
    }

    std::sort(hits.begin(), hits.end(), [](const VectorHit& a, const VectorHit& b) { return a.count > b.count; });
    return hits;
}

std::vector<MemoryRegion> SubtractRange(const std::vector<MemoryRegion>& regions, uintptr_t begin, uintptr_t end) {
    std::vector<MemoryRegion> result;
    for (const MemoryRegion& r : regions) {
        if (end <= r.begin || begin >= r.end || begin >= end) {
            result.push_back(r);        // no overlap
            continue;
        }
        if (begin > r.begin)
            result.push_back({ r.begin, begin });   // the part before the cut
        if (end < r.end)
            result.push_back({ end, r.end });       // the part after the cut
    }
    return result;
}

std::vector<uintptr_t> FindQwordsInRegions(const ReadFn& read, const std::vector<MemoryRegion>& regions,
    const std::vector<uint64_t>& values, std::vector<uint8_t>& scratch, size_t maxHits)
{
    std::vector<uintptr_t> hits;
    const size_t chunk = scratch.size() & ~static_cast<size_t>(7);
    if (!read || values.empty() || chunk < 64 || maxHits == 0)
        return hits;

    for (const MemoryRegion& region : regions) {
        const uintptr_t begin = (region.begin + 7) & ~static_cast<uintptr_t>(7);
        const uintptr_t end = region.end & ~static_cast<uintptr_t>(7);
        for (uintptr_t address = begin; address < end; address += chunk) {
            const size_t size = static_cast<size_t>(std::min<uintptr_t>(chunk, end - address));
            if (!read(address, scratch.data(), size))
                continue;               // a chunk we can't read (freed or protected): skip it

            for (size_t i = 0; i + sizeof(uint64_t) <= size; i += sizeof(uint64_t)) {
                uint64_t value;
                std::memcpy(&value, scratch.data() + i, sizeof(value));
                for (uint64_t wanted : values) {
                    if (value == wanted) {
                        hits.push_back(address + i);
                        if (hits.size() >= maxHits)
                            return hits;
                        break;
                    }
                }
            }
        }
    }
    return hits;
}

std::vector<MemoryRegion> KeepResidentPages(const std::vector<MemoryRegion>& regions, size_t pageSize,
    size_t batchPages, const ResidencyFn& query)
{
    std::vector<MemoryRegion> result;
    if (!query || pageSize == 0 || batchPages == 0)
        return result;                  // can't tell what is resident, so touch nothing

    const uintptr_t pageMask = ~(static_cast<uintptr_t>(pageSize) - 1);
    for (const MemoryRegion& region : regions) {
        MemoryRegion run;
        bool inRun = false;

        uintptr_t page = region.begin & pageMask;
        while (page < region.end) {
            const size_t remaining = static_cast<size_t>((region.end - page + pageSize - 1) / pageSize);
            const size_t count = std::min(batchPages, remaining);
            std::vector<bool> resident(count, false);
            query(page, count, resident);

            for (size_t i = 0; i < count; ++i) {
                if (!resident[i])
                    continue;       // a gap: the open run is only extended by a contiguous page, so it ends by itself
                const uintptr_t pageStart = page + i * pageSize;
                const uintptr_t begin = std::max(pageStart, region.begin);
                const uintptr_t end = std::min(pageStart + pageSize, region.end);
                if (inRun && run.end == begin) {
                    run.end = end;
                }
                else {
                    if (inRun)
                        result.push_back(run);
                    run.begin = begin;
                    run.end = end;
                    inRun = true;
                }
            }
            page += count * pageSize;
        }
        if (inRun)
            result.push_back(run);
    }
    return result;
}

std::vector<std::vector<MemoryRegion>> SliceRegions(const std::vector<MemoryRegion>& regions, size_t maxBytes) {
    std::vector<std::vector<MemoryRegion>> slices;
    if (maxBytes == 0)
        return slices;

    std::vector<MemoryRegion> current;
    size_t used = 0;
    for (const MemoryRegion& region : regions) {
        uintptr_t begin = region.begin;
        while (begin < region.end) {
            const size_t take = static_cast<size_t>(std::min<uintptr_t>(maxBytes - used, region.end - begin));
            current.push_back({ begin, begin + take });
            used += take;
            begin += take;
            if (used == maxBytes) {
                slices.push_back(current);
                current.clear();
                used = 0;
            }
        }
    }
    if (!current.empty())
        slices.push_back(current);
    return slices;
}

std::vector<size_t> FindPointerFields(const ReadFn& read, uintptr_t object, size_t scanBytes,
    const VftablePredicate& isTargetVftable)
{
    std::vector<size_t> offsets;
    if (!read || !isTargetVftable)
        return offsets;

    for (size_t offset = 0; offset + sizeof(uint64_t) <= scanBytes; offset += sizeof(uint64_t)) {
        uint64_t pointer = 0, vftable = 0;
        if (!Read64(read, object + offset, pointer))
            continue;
        if (pointer < kMinPointer || pointer >= kMaxPointer || (pointer & 7) != 0)
            continue;
        if (Read64(read, static_cast<uintptr_t>(pointer), vftable) && isTargetVftable(vftable))
            offsets.push_back(offset);
    }
    return offsets;
}

} // namespace Scan
} // namespace Dinki
