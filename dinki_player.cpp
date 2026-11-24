#include "dinki_player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Dinki {
namespace Player {

namespace {

    bool Contains(const std::vector<uintptr_t>& list, uint64_t value) {
        return std::find(list.begin(), list.end(), static_cast<uintptr_t>(value)) != list.end();
    }

    bool ReadFloat(const Scan::ReadFn& read, uintptr_t address, float& out) {
        return read && read(address, &out, sizeof(out));
    }

    bool IsWhole(float v) {
        return std::fabs(v - std::round(v)) < 1e-3f;
    }

} // namespace

Check InspectPlayer(const Scan::ReadFn& read, uintptr_t address, const Layout& layout,
    const std::vector<uintptr_t>& playerVftables, const std::vector<uintptr_t>& characterVftables)
{
    Check check;
    uint64_t first = 0;
    if (!Scan::Read64(read, address, first))
        return check;
    check.vftableOk = Contains(playerVftables, first);

    uint64_t character = 0;
    if (Scan::Read64(read, address + layout.characterOffset, character)) {
        check.character = static_cast<uintptr_t>(character);
        uint64_t characterVftable = 0;
        check.characterOk = character >= 0x10000 && Scan::Read64(read, static_cast<uintptr_t>(character), characterVftable) &&
            Contains(characterVftables, characterVftable);
    }

    uint64_t self = 0;
    check.selfOk = Scan::Read64(read, address + layout.selfOffset, self) && self == address;
    return check;
}

std::vector<PairHit> FindHealthLikePairs(const Scan::ReadFn& read, uintptr_t base, size_t bytes, size_t maxResults) {
    std::vector<PairHit> whole, other;
    for (size_t offset = 0; offset + 8 <= bytes; offset += 4) {
        float maxHealth = 0.0f, current = 0.0f;
        if (!ReadFloat(read, base + offset, maxHealth) || !ReadFloat(read, base + offset + 4, current))
            continue;
        if (!std::isfinite(maxHealth) || !std::isfinite(current))
            continue;
        if (maxHealth < 1.0f || maxHealth > 100000.0f || current <= 0.0f || current > maxHealth)
            continue;
        PairHit hit;
        hit.offset = offset;
        hit.first = maxHealth;
        hit.second = current;
        (IsWhole(maxHealth) ? whole : other).push_back(hit);
    }
    whole.insert(whole.end(), other.begin(), other.end());
    if (whole.size() > maxResults)
        whole.resize(maxResults);
    return whole;
}

std::vector<TripleHit> FindPositionLikeTriples(const Scan::ReadFn& read, uintptr_t base, size_t bytes, size_t maxResults) {
    std::vector<TripleHit> hits;
    for (size_t offset = 0; offset + 12 <= bytes && hits.size() < maxResults; offset += 4) {
        float v[3] = {};
        if (!ReadFloat(read, base + offset, v[0]) || !ReadFloat(read, base + offset + 4, v[1]) ||
            !ReadFloat(read, base + offset + 8, v[2]))
            continue;
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
            continue;
        if (std::fabs(v[0]) >= 100000.0f || std::fabs(v[1]) >= 100000.0f || std::fabs(v[2]) >= 100000.0f)
            continue;

        int farFromZero = 0;
        for (float f : v)
            if (std::fabs(f) >= 1.0f)
                ++farFromZero;
        if (farFromZero < 2)
            continue;
        if (v[0] == v[1] && v[1] == v[2])
            continue;

        TripleHit hit;
        hit.offset = offset;
        hit.x = v[0];
        hit.y = v[1];
        hit.z = v[2];
        hits.push_back(hit);
    }
    return hits;
}

namespace Vehicle {

uint64_t LearnStartVftable(const Scan::ReadFn& read, const std::vector<FieldPointer>& pointers,
    const std::vector<uintptr_t>& vehicleVftables, uint64_t current, size_t maxGap)
{
    for (const FieldPointer& a : pointers) {
        for (const FieldPointer& b : pointers) {
            if (!(a.pointer < b.pointer && b.pointer - a.pointer <= maxGap))
                continue;
            uint64_t vftable = 0;
            if (Scan::Read64(read, a.pointer, vftable) && Contains(vehicleVftables, vftable))
                current = vftable;
        }
    }
    return current;
}

uintptr_t ResolveStart(const Scan::ReadFn& read, uintptr_t pointer, uint64_t startVftable, size_t maxBack,
    bool& adjusted)
{
    adjusted = false;
    uint64_t first = 0;
    if (Scan::Read64(read, pointer, first) && first == startVftable)
        return pointer;                 // already the start

    for (size_t back = 8; back <= maxBack; back += 8) {
        uint64_t vftable = 0;
        if (pointer > back + 0x10000 && Scan::Read64(read, pointer - back, vftable) && vftable == startVftable) {
            adjusted = true;
            return pointer - back;
        }
    }
    return pointer;                     // couldn't find the start: leave it alone
}

std::vector<Group> GroupPointers(const Scan::ReadFn& read, const std::vector<FieldPointer>& pointers,
    uint64_t startVftable, size_t maxBack)
{
    std::vector<Group> groups;
    for (const FieldPointer& p : pointers) {
        bool adjusted = false;
        const uintptr_t base = ResolveStart(read, p.pointer, startVftable, maxBack, adjusted);

        Group* existing = nullptr;
        for (Group& g : groups)
            if (g.base == base)
                existing = &g;
        if (existing) {
            existing->fields.push_back(p.field);
            existing->adjusted = existing->adjusted || adjusted;
        }
        else {
            Group g;
            g.base = base;
            g.fields.push_back(p.field);
            g.adjusted = adjusted;
            groups.push_back(g);
        }
    }
    return groups;
}

} // namespace Vehicle

} // namespace Player
} // namespace Dinki
