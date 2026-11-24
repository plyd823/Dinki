// Standalone tests for dinki_build.cpp and dinki_scan.cpp. No game, no Windows needed.
//
//   g++ -std=c++20 -I. tests/dinki_gog_test.cpp dinki_build.cpp dinki_scan.cpp dinki_player.cpp -o dinki_gog_test && ./dinki_gog_test
//   cl /std:c++20 /EHsc /I. tests\dinki_gog_test.cpp dinki_build.cpp dinki_scan.cpp dinki_player.cpp && dinki_gog_test.exe

#include "dinki_build.h"
#include "dinki_player.h"
#include "dinki_scan.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failures;                                                            \
            std::printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                            \
    } while (0)

using namespace Dinki;
using namespace Dinki::Scan;

// ---- fake process memory ----
struct FakeMemory {
    struct Region {
        uintptr_t base;
        std::vector<uint8_t> bytes;
    };
    std::vector<Region> regions;
    mutable std::vector<uintptr_t> reads;   // every address anything tried to read

    void Add(uintptr_t base, size_t size) { regions.push_back({ base, std::vector<uint8_t>(size, 0) }); }

    void PutFloat(uintptr_t address, float value) {
        for (Region& r : regions)
            if (address >= r.base && address + 4 <= r.base + r.bytes.size()) {
                std::memcpy(&r.bytes[address - r.base], &value, 4);
                return;
            }
        std::printf("test setup error: PutFloat outside any region 0x%llX\n", static_cast<unsigned long long>(address));
        ++g_failures;
    }

    void Put64(uintptr_t address, uint64_t value) {
        for (Region& r : regions)
            if (address >= r.base && address + 8 <= r.base + r.bytes.size()) {
                std::memcpy(&r.bytes[address - r.base], &value, 8);
                return;
            }
        std::printf("test setup error: Put64 outside any region 0x%llX\n", static_cast<unsigned long long>(address));
        ++g_failures;
    }

    bool Read(uintptr_t address, void* out, size_t size) const {
        reads.push_back(address);
        for (const Region& r : regions)
            if (address >= r.base && address + size <= r.base + r.bytes.size()) {
                std::memcpy(out, &r.bytes[address - r.base], size);
                return true;
            }
        return false;
    }

    ReadFn Reader() const { return [this](uintptr_t a, void* o, size_t s) { return Read(a, o, s); }; }

    bool WasRead(uintptr_t begin, uintptr_t end) const {
        for (uintptr_t a : reads)
            if (a >= begin && a < end)
                return true;
        return false;
    }
};

constexpr Range kImage = { 0x140000000, 0x141AAE000 };
constexpr Range kData  = { 0x1416BC000, 0x1416BC000 + 0x1000 };   // a small stand-in for .data
constexpr uintptr_t kVft = 0x14121E460;                            // some class's vftable (inside the image)
constexpr uintptr_t kMissionVft = 0x14121B640;

static FakeMemory NewMemory() {
    FakeMemory m;
    m.Add(kData.begin, kData.end - kData.begin);
    return m;
}

// ------------------------------------------------------------------ build

static void TestClassifyBuild() {
    CHECK(ClassifyBuild(0x565D5965, 0x01AAE000) == Build::Gog2015);
    CHECK(ClassifyBuild(0x565D5966, 0x01AAE000) == Build::Legacy);   // different timestamp
    CHECK(ClassifyBuild(0x565D5965, 0x01AAE001) == Build::Legacy);   // different size
    CHECK(ClassifyBuild(0, 0) == Build::Legacy);
    CHECK(ClassifyBuild(0xFFFFFFFF, 0xFFFFFFFF) == Build::Legacy);
    CHECK(kGog2015Timestamp == 0x565D5965 && kGog2015SizeOfImage == 0x1AAE000);   // matches the logged exe
#ifndef _WIN32
    CHECK(GetBuild() == Build::Legacy);
    CHECK(!IsGog());
#endif
}

// ------------------------------------------------------------------ FindObjects

static void TestHeapObject() {
    FakeMemory m = NewMemory();
    m.Add(0x7FF600001000, 0x100);
    m.Put64(0x7FF600001000, kVft);
    m.Put64(kData.begin + 0x40, 0x7FF600001000);   // a global pointer to it

    const auto hits = FindObjects(m.Reader(), kData, kImage, kVft, { 0 });
    CHECK(hits.size() == 1);
    CHECK(hits[0].base == 0x7FF600001000);
    CHECK(hits[0].vftableOffset == 0);
    CHECK(!hits[0].isStatic);
    CHECK(hits[0].slot == kData.begin + 0x40);
    CHECK(hits[0].refs == 1);
}

static void TestEmbeddedObject() {
    FakeMemory m = NewMemory();
    m.Add(0x7FF600002000, 0x200);
    m.Put64(0x7FF600002000 + 0x70, kVft);          // the class sits 0x70 bytes into a bigger object
    m.Put64(kData.begin + 0x80, 0x7FF600002000);

    const auto withOffset = FindObjects(m.Reader(), kData, kImage, kVft, { 0, 0x70, 0xA0 });
    CHECK(withOffset.size() == 1);
    CHECK(withOffset[0].base == 0x7FF600002000);
    CHECK(withOffset[0].vftableOffset == 0x70);

    const auto withoutOffset = FindObjects(m.Reader(), kData, kImage, kVft, { 0 });
    CHECK(withoutOffset.empty());                  // only found when the right offset is allowed
}

static void TestStaticObject() {
    FakeMemory m = NewMemory();
    m.Put64(kData.begin + 0x100, kVft);            // the object lives inside .data itself

    const auto hits = FindObjects(m.Reader(), kData, kImage, kVft, { 0 });
    CHECK(hits.size() == 1);
    CHECK(hits[0].isStatic);
    CHECK(hits[0].base == kData.begin + 0x100);
}

static void TestWrongVftableIgnored() {
    FakeMemory m = NewMemory();
    m.Add(0x7FF600003000, 0x100);
    m.Put64(0x7FF600003000, 0x14121E999);          // some other class
    m.Put64(kData.begin + 8, 0x7FF600003000);
    CHECK(FindObjects(m.Reader(), kData, kImage, kVft, { 0 }).empty());
}

static void TestPointersIntoTheExeAreNeverFollowed() {
    FakeMemory m = NewMemory();
    m.Put64(kData.begin + 0x10, 0x140100000);      // points into code/constants
    m.Put64(kData.begin + 0x18, 0x141AAD000);      // points near the end of the image
    FindObjects(m.Reader(), kData, kImage, kVft, { 0, 0x70 });
    // .data is part of the image, so only look at the code/constant parts around it
    CHECK(!m.WasRead(kImage.begin, kData.begin));
    CHECK(!m.WasRead(kData.end, kImage.end));      // never dereferenced a pointer into the image
}

static void TestJunkValuesAreNotFollowed() {
    FakeMemory m = NewMemory();
    m.Put64(kData.begin + 0x10, 0);                // null
    m.Put64(kData.begin + 0x18, 5);                // small integer
    m.Put64(kData.begin + 0x20, 0xFFFFFFFFFFFFFFFFull);   // kernel-range value
    m.Put64(kData.begin + 0x28, 0x7FF600001004);   // misaligned
    FindObjects(m.Reader(), kData, kImage, kVft, { 0 });
    CHECK(!m.WasRead(0, 0x10000));
    CHECK(!m.WasRead(0x7FF600001004, 0x7FF600001005));
    CHECK(!m.WasRead(0xFFFFFFFFFFFFFFF0ull, 0xFFFFFFFFFFFFFFFFull));
}

static void TestUnreadableHeapPointerIsSkipped() {
    FakeMemory m = NewMemory();
    m.Put64(kData.begin + 0x10, 0x7FF6DEAD0000);  // dangling: nothing mapped there
    m.Add(0x7FF600004000, 0x100);
    m.Put64(0x7FF600004000, kVft);
    m.Put64(kData.begin + 0x18, 0x7FF600004000);
    const auto hits = FindObjects(m.Reader(), kData, kImage, kVft, { 0 });
    CHECK(hits.size() == 1);                       // the dangling one is skipped, the good one is found
    CHECK(hits[0].base == 0x7FF600004000);
}

static void TestSeveralSlotsOneObject() {
    FakeMemory m = NewMemory();
    m.Add(0x7FF600005000, 0x100);
    m.Put64(0x7FF600005000, kVft);
    m.Put64(kData.begin + 0x10, 0x7FF600005000);
    m.Put64(kData.begin + 0x90, 0x7FF600005000);
    const auto hits = FindObjects(m.Reader(), kData, kImage, kVft, { 0 });
    CHECK(hits.size() == 1);
    CHECK(hits[0].refs == 2);
    CHECK(hits[0].slot == kData.begin + 0x10);     // the first slot is kept
}

static void TestTwoDifferentObjects() {
    FakeMemory m = NewMemory();
    m.Add(0x7FF600006000, 0x100);
    m.Add(0x7FF600007000, 0x100);
    m.Put64(0x7FF600006000, kVft);
    m.Put64(0x7FF600007000, kVft);
    m.Put64(kData.begin + 0x10, 0x7FF600006000);
    m.Put64(kData.begin + 0x18, 0x7FF600007000);
    CHECK(FindObjects(m.Reader(), kData, kImage, kVft, { 0 }).size() == 2);
}

static void TestNullReader() {
    CHECK(FindObjects(nullptr, kData, kImage, kVft, { 0 }).empty());
    CHECK(FindPointerVectors(nullptr, 0x1000, 0x100, [](uint64_t) { return true; }).empty());
}

// ------------------------------------------------------------------ FindPointerVectors

// Builds a manager object at `object` holding a vector<T*> at `vectorOffset` with `count` elements.
// `badElement` (if >= 0) gets the wrong vftable.
static void BuildVector(FakeMemory& m, uintptr_t object, size_t vectorOffset, uintptr_t array, size_t count,
    int badElement = -1)
{
    for (size_t i = 0; i < count; ++i) {
        const uintptr_t element = array + 0x10000 + i * 0x40;
        m.Add(element, 0x40);
        m.Put64(element, (static_cast<int>(i) == badElement) ? 0x14121B999 : kMissionVft);
        m.Put64(array + i * 8, element);
    }
    m.Put64(object + vectorOffset, array);
    m.Put64(object + vectorOffset + 8, array + count * 8);
    m.Put64(object + vectorOffset + 16, array + count * 8 + 16);   // capacity a little larger
}

static const VftablePredicate kIsMission = [](uint64_t v) { return v == kMissionVft; };

static void TestFindsAVector() {
    FakeMemory m;
    m.Add(0x7FF600010000, 0x200);
    m.Add(0x7FF600020000, 0x10000);
    BuildVector(m, 0x7FF600010000, 0x40, 0x7FF600020000, 5);

    const auto hits = FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, kIsMission);
    CHECK(hits.size() == 1);
    CHECK(hits[0].offset == 0x40);
    CHECK(hits[0].count == 5);
    CHECK(hits[0].begin == 0x7FF600020000);
}

static void TestBadElementRejectsTheVector() {
    FakeMemory m;
    m.Add(0x7FF600010000, 0x200);
    m.Add(0x7FF600020000, 0x10000);
    BuildVector(m, 0x7FF600010000, 0x40, 0x7FF600020000, 5, /*badElement=*/3);
    CHECK(FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, kIsMission).empty());
}

static void TestEmptyVectorIsNotReported() {
    FakeMemory m;
    m.Add(0x7FF600010000, 0x200);
    m.Put64(0x7FF600010000 + 0x40, 0x7FF600020000);
    m.Put64(0x7FF600010000 + 0x48, 0x7FF600020000);   // first == last
    m.Put64(0x7FF600010000 + 0x50, 0x7FF600020040);
    CHECK(FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, kIsMission).empty());
}

static void TestGarbageIsNotAVector() {
    FakeMemory m;
    m.Add(0x7FF600010000, 0x200);
    for (uintptr_t o = 0; o < 0x200; o += 8)
        m.Put64(0x7FF600010000 + o, 0x1234567800000000ull + o * 977);
    CHECK(FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, kIsMission).empty());
}

static void TestInvertedPointersRejected() {
    FakeMemory m;
    m.Add(0x7FF600010000, 0x200);
    m.Add(0x7FF600020000, 0x10000);
    BuildVector(m, 0x7FF600010000, 0x40, 0x7FF600020000, 4);
    m.Put64(0x7FF600010000 + 0x50, 0x7FF600020000);   // end-of-capacity now before last: not a vector
    CHECK(FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, kIsMission).empty());
}

static void TestLargeVectorEveryElementChecked() {
    {
        FakeMemory m;
        m.Add(0x7FF600010000, 0x200);
        m.Add(0x7FF600100000, 0x200000);
        BuildVector(m, 0x7FF600010000, 0x20, 0x7FF600100000, 300);
        const auto hits = FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, kIsMission);
        CHECK(hits.size() == 1 && hits[0].count == 300);
    }
    {
        FakeMemory m;
        m.Add(0x7FF600010000, 0x200);
        m.Add(0x7FF600100000, 0x200000);
        BuildVector(m, 0x7FF600010000, 0x20, 0x7FF600100000, 300, /*badElement=*/150);   // in the middle
        CHECK(FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, kIsMission).empty());
    }
}

static void TestSeveralVectorsSortedByCount() {
    FakeMemory m;
    m.Add(0x7FF600010000, 0x200);
    m.Add(0x7FF600020000, 0x10000);
    m.Add(0x7FF600040000, 0x10000);
    BuildVector(m, 0x7FF600010000, 0x30, 0x7FF600020000, 5);
    BuildVector(m, 0x7FF600010000, 0x90, 0x7FF600040000, 12);
    const auto hits = FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, kIsMission);
    CHECK(hits.size() == 2);
    CHECK(hits[0].count == 12 && hits[0].offset == 0x90);
    CHECK(hits[1].count == 5 && hits[1].offset == 0x30);
}

static void TestScanBeyondTheObjectDoesNotCrash() {
    FakeMemory m;
    m.Add(0x7FF600010000, 0x100);                     // only 0x100 mapped, but we ask to scan 0x400
    m.Add(0x7FF600020000, 0x10000);
    BuildVector(m, 0x7FF600010000, 0x40, 0x7FF600020000, 3);
    const auto hits = FindPointerVectors(m.Reader(), 0x7FF600010000, 0x400, kIsMission);
    CHECK(hits.size() == 1 && hits[0].count == 3);    // still found; the unreadable tail is skipped
}

static void TestPredicateCanAcceptARange() {
    FakeMemory m;
    m.Add(0x7FF600010000, 0x200);
    m.Add(0x7FF600020000, 0x10000);
    BuildVector(m, 0x7FF600010000, 0x40, 0x7FF600020000, 4, /*badElement=*/2);   // bad one has 0x14121B999
    // "anything that looks like a vftable in .rdata" accepts both vftable values
    const auto anyVftable = [](uint64_t v) { return v >= 0x141198000 && v < 0x141700000; };
    const auto hits = FindPointerVectors(m.Reader(), 0x7FF600010000, 0x200, anyVftable);
    CHECK(hits.size() == 1 && hits[0].count == 4);
}


// ------------------------------------------------------------------ FindObjectsMulti

constexpr uintptr_t kVftB = 0x14121E478;   // a second vftable (the second base of a multiple-inheritance class)
constexpr uintptr_t kVftC = 0x1411C1660;   // an unrelated class

static void TestMultiFindsSeveralClassesInOnePass() {
    FakeMemory m = NewMemory();
    m.Add(0x7FF600008000, 0x100);
    m.Add(0x7FF600009000, 0x100);
    m.Put64(0x7FF600008000, kVft);
    m.Put64(0x7FF600009000, kVftC);
    m.Put64(kData.begin + 0x10, 0x7FF600008000);
    m.Put64(kData.begin + 0x18, 0x7FF600009000);

    const auto hits = FindObjectsMulti(m.Reader(), kData, kImage, { kVft, kVftB, kVftC }, { 0 });
    CHECK(hits.size() == 2);
    int a = 0, c = 0;
    for (const MultiHit& h : hits) {
        if (h.vftable == kVft && h.hit.base == 0x7FF600008000) ++a;
        if (h.vftable == kVftC && h.hit.base == 0x7FF600009000) ++c;
    }
    CHECK(a == 1 && c == 1);
}

static void TestMultiFindsBothBasesOfOneObject() {
    FakeMemory m = NewMemory();
    m.Add(0x7FF60000A000, 0x100);
    m.Put64(0x7FF60000A000 + 0x00, kVftB);     // first base
    m.Put64(0x7FF60000A000 + 0x20, kVft);      // second base, further into the same object
    m.Put64(kData.begin + 0x20, 0x7FF60000A000);

    const auto hits = FindObjectsMulti(m.Reader(), kData, kImage, { kVft, kVftB }, { 0, 0x10, 0x20 });
    CHECK(hits.size() == 2);                    // one object, two hits: both bases seen
    bool sameBase = true;
    for (const MultiHit& h : hits)
        sameBase = sameBase && h.hit.base == 0x7FF60000A000;
    CHECK(sameBase);
}

static void TestMultiReadsEachPointerOncePerOffset() {
    // The cost of a pass must not grow with the number of classes being searched for.
    auto countReads = [](size_t classes) {
        FakeMemory m = NewMemory();
        m.Add(0x7FF60000B000, 0x100);
        m.Put64(kData.begin + 0x10, 0x7FF60000B000);
        std::vector<uintptr_t> vfts;
        for (size_t i = 0; i < classes; ++i)
            vfts.push_back(0x141200000 + i * 0x20);
        FindObjectsMulti(m.Reader(), kData, kImage, vfts, { 0, 0x10, 0x20 });
        size_t inHeap = 0;
        for (uintptr_t a : m.reads)
            if (a >= 0x7FF60000B000 && a < 0x7FF60000B100)
                ++inHeap;
        return inHeap;
    };
    CHECK(countReads(1) == 3);
    CHECK(countReads(40) == 3);                 // same number of heap reads with 40 classes
}

static void TestMultiNoVftablesOrNoReader() {
    FakeMemory m = NewMemory();
    CHECK(FindObjectsMulti(m.Reader(), kData, kImage, {}, { 0 }).empty());
    CHECK(FindObjectsMulti(nullptr, kData, kImage, { kVft }, { 0 }).empty());
}

static void TestMultiStaticObjectOfASpecificClass() {
    FakeMemory m = NewMemory();
    m.Put64(kData.begin + 0x200, kVftC);
    const auto hits = FindObjectsMulti(m.Reader(), kData, kImage, { kVft, kVftC }, { 0 });
    CHECK(hits.size() == 1 && hits[0].vftable == kVftC && hits[0].hit.isStatic && hits[0].hit.base == kData.begin + 0x200);
}


// ------------------------------------------------------------------ SubtractRange / FindQwordsInRegions / FindPointerFields

static bool SameRegions(const std::vector<MemoryRegion>& got, const std::vector<MemoryRegion>& want) {
    if (got.size() != want.size())
        return false;
    for (size_t i = 0; i < got.size(); ++i)
        if (got[i].begin != want[i].begin || got[i].end != want[i].end)
            return false;
    return true;
}

static void TestSubtractRange() {
    const std::vector<MemoryRegion> one = { { 100, 200 } };
    CHECK(SameRegions(SubtractRange(one, 120, 150), { { 100, 120 }, { 150, 200 } }));   // a hole in the middle
    CHECK(SameRegions(SubtractRange(one, 0, 50), one));                                  // entirely before
    CHECK(SameRegions(SubtractRange(one, 250, 300), one));                               // entirely after
    CHECK(SameRegions(SubtractRange(one, 0, 100), one));                                 // touches the start only
    CHECK(SameRegions(SubtractRange(one, 200, 300), one));                               // touches the end only
    CHECK(SubtractRange(one, 50, 250).empty());                                          // covers all of it
    CHECK(SameRegions(SubtractRange(one, 50, 150), { { 150, 200 } }));                   // cuts the front
    CHECK(SameRegions(SubtractRange(one, 150, 250), { { 100, 150 } }));                  // cuts the back
    CHECK(SameRegions(SubtractRange(one, 150, 150), one));                               // empty cut
    CHECK(SameRegions(SubtractRange({ { 100, 200 }, { 300, 400 } }, 150, 350),
        { { 100, 150 }, { 350, 400 } }));                                                // spans two regions
}

constexpr uint64_t kScanValue = 0x141231A10;

static void TestFindQwordsAcrossChunks() {
    FakeMemory m;
    m.Add(0x7FF600100000, 0x400);
    m.Put64(0x7FF600100010, kScanValue);
    m.Put64(0x7FF600100100, kScanValue);           // exactly at the start of the second chunk
    m.Put64(0x7FF6001001F8, kScanValue);           // the last qword of the second chunk
    m.Put64(0x7FF600100300, 0x141231A38);          // a second value we also look for

    std::vector<uint8_t> scratch(0x100);
    const auto hits = FindQwordsInRegions(m.Reader(), { { 0x7FF600100000, 0x7FF600100400 } },
        { kScanValue, 0x141231A38 }, scratch, 100);
    CHECK(hits.size() == 4);
    CHECK(hits[0] == 0x7FF600100010 && hits[1] == 0x7FF600100100 && hits[2] == 0x7FF6001001F8 && hits[3] == 0x7FF600100300);
}

static void TestFindQwordsLimitsAndGuards() {
    FakeMemory m;
    m.Add(0x7FF600110000, 0x400);
    for (uintptr_t a = 0x7FF600110000; a < 0x7FF600110100; a += 8)
        m.Put64(a, kScanValue);                    // 32 hits available

    std::vector<uint8_t> scratch(0x100);
    CHECK(FindQwordsInRegions(m.Reader(), { { 0x7FF600110000, 0x7FF600110400 } }, { kScanValue }, scratch, 5).size() == 5);

    std::vector<uint8_t> tiny(16);
    CHECK(FindQwordsInRegions(m.Reader(), { { 0x7FF600110000, 0x7FF600110400 } }, { kScanValue }, tiny, 5).empty());
    CHECK(FindQwordsInRegions(m.Reader(), { { 0x7FF600110000, 0x7FF600110400 } }, {}, scratch, 5).empty());
    CHECK(FindQwordsInRegions(m.Reader(), { { 0x7FF600110000, 0x7FF600110400 } }, { kScanValue }, scratch, 0).empty());
    CHECK(FindQwordsInRegions(nullptr, { { 0x7FF600110000, 0x7FF600110400 } }, { kScanValue }, scratch, 5).empty());
}

static void TestFindQwordsSkipsUnreadableChunks() {
    FakeMemory m;
    m.Add(0x7FF600120000, 0x100);                  // readable
    m.Add(0x7FF600120200, 0x100);                  // readable, after a gap that is NOT mapped
    m.Put64(0x7FF600120010, kScanValue);
    m.Put64(0x7FF600120210, kScanValue);

    std::vector<uint8_t> scratch(0x100);
    // the region claims to cover the unmapped gap too; the chunk over the gap can't be read and is skipped
    const auto hits = FindQwordsInRegions(m.Reader(), { { 0x7FF600120000, 0x7FF600120300 } }, { kScanValue }, scratch, 10);
    CHECK(hits.size() == 2);
}

static void TestFindQwordsAlignsRegionEdges() {
    FakeMemory m;
    m.Add(0x7FF600130000, 0x200);
    m.Put64(0x7FF600130008, kScanValue);
    std::vector<uint8_t> scratch(0x100);
    // a region that starts and ends off a multiple of 8 must not read outside it or crash
    const auto hits = FindQwordsInRegions(m.Reader(), { { 0x7FF600130005, 0x7FF600130103 } }, { kScanValue }, scratch, 10);
    CHECK(hits.size() == 1 && hits[0] == 0x7FF600130008);
}

static void TestScannerCanExcludeItsOwnBuffer() {
    // The scan copies memory into `scratch`. If the scratch buffer sits inside a scanned region, the copy would
    // be found too. Cutting the buffer out of the regions (SubtractRange) prevents that.
    FakeMemory m;
    m.Add(0x7FF600140000, 0x1000);                 // includes the "scratch" at 0x7FF600140800
    m.Put64(0x7FF600140100, kScanValue);           // the real object
    m.Put64(0x7FF600140808, kScanValue);           // what a stale copy in the scratch buffer would look like

    std::vector<uint8_t> scratch(0x100);
    const auto regions = SubtractRange({ { 0x7FF600140000, 0x7FF600141000 } }, 0x7FF600140800, 0x7FF600140900);
    const auto hits = FindQwordsInRegions(m.Reader(), regions, { kScanValue }, scratch, 10);
    CHECK(hits.size() == 1 && hits[0] == 0x7FF600140100);
}

static void TestFindPointerFields() {
    FakeMemory m;
    m.Add(0x7FF600200000, 0x100);
    const uintptr_t carA = 0x7FF600210000, carB = 0x7FF600211000, other = 0x7FF600212000;
    m.Add(carA, 0x40); m.Add(carB, 0x40); m.Add(other, 0x40);
    m.Put64(carA, 0x141249810);
    m.Put64(carB, 0x141249A38);
    m.Put64(other, 0x14121B640);                   // some other class
    m.Put64(0x7FF600200000 + 0x10, carA);
    m.Put64(0x7FF600200000 + 0x20, 12345);         // not a pointer
    m.Put64(0x7FF600200000 + 0x30, 0x7FF6DEAD0000);   // dangling pointer
    m.Put64(0x7FF600200000 + 0x40, carB);
    m.Put64(0x7FF600200000 + 0x50, other);
    m.Add(0x7FF600213004, 0x40);                   // a real car object, but at an address that isn't 8-aligned
    m.Put64(0x7FF600213004, 0x141249810);
    m.Put64(0x7FF600200000 + 0x58, 0x7FF600213004);   // misaligned pointer: must be ignored
    const auto isCar = [](uint64_t v) { return v == 0x141249810 || v == 0x141249A38; };

    const auto offsets = FindPointerFields(m.Reader(), 0x7FF600200000, 0x100, isCar);
    CHECK(offsets.size() == 2 && offsets[0] == 0x10 && offsets[1] == 0x40);
    CHECK(FindPointerFields(m.Reader(), 0x7FF600200000, 0x100, nullptr).empty());
    CHECK(FindPointerFields(nullptr, 0x7FF600200000, 0x100, isCar).empty());
    CHECK(FindPointerFields(m.Reader(), 0x7FF600200000, 0x18, isCar).size() == 1);   // only scans what it was asked to
}

// ------------------------------------------------------------------ Player

using namespace Dinki::Player;

static const std::vector<uintptr_t> kPlayerVfts = { 0x141231A10, 0x141231A38, 0x141231B10 };
static const std::vector<uintptr_t> kCharacterVfts = { 0x1411C0B40, 0x1411C0B58, 0x1411C0B78 };

static FakeMemory PlayerWorld(uintptr_t player, uintptr_t character) {
    FakeMemory m;
    m.Add(player, 0x100);
    m.Add(character, 0x100);
    m.Put64(player + 0x00, 0x141231A10);
    m.Put64(player + 0x10, character);
    m.Put64(player + 0x18, player);
    m.Put64(character, 0x1411C0B58);
    return m;
}

static void TestInspectPlayerGood() {
    FakeMemory m = PlayerWorld(0x7FF600300000, 0x7FF600301000);
    const Check c = InspectPlayer(m.Reader(), 0x7FF600300000, Layout(), kPlayerVfts, kCharacterVfts);
    CHECK(c.vftableOk && c.characterOk && c.selfOk);
    CHECK(c.Plausible());
    CHECK(c.Score() == 4);
    CHECK(c.character == 0x7FF600301000);
}

static void TestInspectPlayerBad() {
    {   // wrong class at the top
        FakeMemory m = PlayerWorld(0x7FF600300000, 0x7FF600301000);
        m.Put64(0x7FF600300000, 0x14121E460);
        const Check c = InspectPlayer(m.Reader(), 0x7FF600300000, Layout(), kPlayerVfts, kCharacterVfts);
        CHECK(!c.vftableOk && c.characterOk && !c.Plausible());
    }
    {   // the character pointer leads to the wrong kind of object
        FakeMemory m = PlayerWorld(0x7FF600300000, 0x7FF600301000);
        m.Put64(0x7FF600301000, 0x141249810);
        const Check c = InspectPlayer(m.Reader(), 0x7FF600300000, Layout(), kPlayerVfts, kCharacterVfts);
        CHECK(c.vftableOk && !c.characterOk && !c.Plausible());
    }
    {   // the self pointer is wrong, but the rest holds: still plausible, lower score
        FakeMemory m = PlayerWorld(0x7FF600300000, 0x7FF600301000);
        m.Put64(0x7FF600300018, 0x7FF600399999);
        const Check c = InspectPlayer(m.Reader(), 0x7FF600300000, Layout(), kPlayerVfts, kCharacterVfts);
        CHECK(c.Plausible() && !c.selfOk && c.Score() == 3);
    }
    {   // null and dangling character pointers
        FakeMemory m = PlayerWorld(0x7FF600300000, 0x7FF600301000);
        m.Put64(0x7FF600300010, 0);
        CHECK(!InspectPlayer(m.Reader(), 0x7FF600300000, Layout(), kPlayerVfts, kCharacterVfts).characterOk);
        m.Put64(0x7FF600300010, 0x7FF6DEAD0000);
        CHECK(!InspectPlayer(m.Reader(), 0x7FF600300000, Layout(), kPlayerVfts, kCharacterVfts).characterOk);
    }
    {   // unreadable object
        FakeMemory m;
        const Check c = InspectPlayer(m.Reader(), 0x7FF600300000, Layout(), kPlayerVfts, kCharacterVfts);
        CHECK(!c.vftableOk && !c.characterOk && !c.selfOk && c.Score() == 0);
    }
}

static void TestInspectPlayerUsesTheLayout() {
    FakeMemory m;
    m.Add(0x7FF600310000, 0x100);
    m.Add(0x7FF600311000, 0x100);
    m.Put64(0x7FF600310000, 0x141231B10);
    m.Put64(0x7FF600310000 + 0x28, 0x7FF600311000);   // a different layout: character at +0x28
    m.Put64(0x7FF600310000 + 0x30, 0x7FF600310000);   // self at +0x30
    m.Put64(0x7FF600311000, 0x1411C0B78);
    Layout layout;
    layout.characterOffset = 0x28;
    layout.selfOffset = 0x30;
    CHECK(InspectPlayer(m.Reader(), 0x7FF600310000, layout, kPlayerVfts, kCharacterVfts).Score() == 4);
    CHECK(!InspectPlayer(m.Reader(), 0x7FF600310000, Layout(), kPlayerVfts, kCharacterVfts).characterOk);   // default layout misses it
}

static void TestHealthLikePairs() {
    FakeMemory m;
    m.Add(0x7FF600400000, 0x400);
    const uintptr_t b = 0x7FF600400000;
    m.PutFloat(b + 0x100, 150.5f);  m.PutFloat(b + 0x104, 120.0f);      // plausible, but a fractional maximum, EARLIER in memory
    m.PutFloat(b + 0x180, 100.0f);  m.PutFloat(b + 0x184, 100.0f);      // the real health: (max, current)
    m.PutFloat(b + 0x240, 100.0f);  m.PutFloat(b + 0x244, 120.0f);      // current above max: rejected
    m.PutFloat(b + 0x260, 0.5f);    m.PutFloat(b + 0x264, 0.25f);       // max below 1: rejected
    m.PutFloat(b + 0x280, std::numeric_limits<float>::quiet_NaN()); m.PutFloat(b + 0x284, 50.0f);   // NaN: rejected
    m.PutFloat(b + 0x2A0, 1e9f);    m.PutFloat(b + 0x2A4, 1.0f);        // absurd max: rejected
    m.PutFloat(b + 0x2C0, 200.0f);  m.PutFloat(b + 0x2C4, 0.0f);        // dead: rejected (current must be above 0)

    const auto hits = FindHealthLikePairs(m.Reader(), b, 0x400, 50);
    // (100,100) matches at 0x180; the pair starting at 0x184 is (100, 0), rejected. Whole-number maxima are listed
    // first even though the fractional pair (0x100) comes earlier in memory.
    CHECK(hits.size() == 2);
    CHECK(hits[0].offset == 0x180 && hits[0].first == 100.0f && hits[0].second == 100.0f);
    CHECK(hits[1].offset == 0x100 && hits[1].first == 150.5f);
    CHECK(FindHealthLikePairs(m.Reader(), b, 0x400, 1).size() == 1);                    // maxResults
    CHECK(FindHealthLikePairs(nullptr, b, 0x400, 10).empty());
}

static void TestPositionLikeTriples() {
    FakeMemory m;
    m.Add(0x7FF600410000, 0x400);
    const uintptr_t b = 0x7FF600410000;
    m.PutFloat(b + 0x208, 1200.5f);  m.PutFloat(b + 0x20C, 35.25f);  m.PutFloat(b + 0x210, -840.75f);   // a position
    m.PutFloat(b + 0x230, 0.0f);     m.PutFloat(b + 0x234, 0.0f);    m.PutFloat(b + 0x238, 1.0f);       // a direction: only one component away from zero
    m.PutFloat(b + 0x250, 0.1f);     m.PutFloat(b + 0x254, 0.2f);    m.PutFloat(b + 0x258, 0.3f);       // tiny values
    m.PutFloat(b + 0x270, 5.0f);     m.PutFloat(b + 0x274, 5.0f);    m.PutFloat(b + 0x278, 5.0f);       // all equal
    m.PutFloat(b + 0x290, 1e6f);     m.PutFloat(b + 0x294, 1.0f);    m.PutFloat(b + 0x298, 2.0f);       // too large
    m.PutFloat(b + 0x2B0, std::numeric_limits<float>::infinity()); m.PutFloat(b + 0x2B4, 3.0f); m.PutFloat(b + 0x2B8, 4.0f);   // infinite

    const auto hits = FindPositionLikeTriples(m.Reader(), b, 0x400, 50);
    bool foundReal = false;
    for (const TripleHit& h : hits)
        if (h.offset == 0x208 && h.x == 1200.5f && h.y == 35.25f && h.z == -840.75f)
            foundReal = true;
    CHECK(foundReal);
    for (const TripleHit& h : hits) {
        CHECK(h.offset != 0x230 && h.offset != 0x250 && h.offset != 0x270 && h.offset != 0x290 && h.offset != 0x2B0);
    }
    CHECK(FindPositionLikeTriples(m.Reader(), b, 0x400, 1).size() <= 1);               // maxResults
}


// ------------------------------------------------------------------ KeepResidentPages / SliceRegions

constexpr size_t kPage = 0x1000;

// Builds a residency query from a set of resident page numbers (address / kPage), counting how often it is asked.
struct FakeResidency {
    std::vector<uintptr_t> residentPages;
    mutable size_t calls = 0;
    mutable size_t pagesAsked = 0;
    ResidencyFn Fn() const {
        return [this](uintptr_t first, size_t count, std::vector<bool>& out) {
            ++calls;
            pagesAsked += count;
            for (size_t i = 0; i < count; ++i)
                for (uintptr_t p : residentPages)
                    if (p == first / kPage + i)
                        out[i] = true;
        };
    }
};

static void TestAllResident() {
    FakeResidency r;
    for (uintptr_t p = 0x100; p < 0x110; ++p) r.residentPages.push_back(p);
    const auto kept = KeepResidentPages({ { 0x100000, 0x110000 } }, kPage, 4, r.Fn());
    CHECK(SameRegions(kept, { { 0x100000, 0x110000 } }));       // one run even though it was queried in batches of 4
    CHECK(r.pagesAsked == 16);
}

static void TestNothingResident() {
    FakeResidency r;
    CHECK(KeepResidentPages({ { 0x100000, 0x110000 } }, kPage, 4, r.Fn()).empty());
}

static void TestResidentGapsSplitRuns() {
    FakeResidency r;
    r.residentPages = { 0x100, 0x101, 0x104, 0x105, 0x106, 0x10F };   // pages 2-3 and 7-14 are not resident
    const auto kept = KeepResidentPages({ { 0x100000, 0x110000 } }, kPage, 3, r.Fn());
    CHECK(SameRegions(kept, { { 0x100000, 0x102000 }, { 0x104000, 0x107000 }, { 0x10F000, 0x110000 } }));
}

static void TestRunsJoinAcrossBatches() {
    FakeResidency r;
    r.residentPages = { 0x102, 0x103, 0x104, 0x105 };                  // spans the batch boundaries at 3 and 6
    CHECK(SameRegions(KeepResidentPages({ { 0x100000, 0x108000 } }, kPage, 3, r.Fn()), { { 0x102000, 0x106000 } }));
}

static void TestResidencyClipsToTheRegion() {
    FakeResidency r;
    for (uintptr_t p = 0x100; p < 0x108; ++p) r.residentPages.push_back(p);
    // a region that doesn't start or end on a page boundary must not be widened to whole pages
    CHECK(SameRegions(KeepResidentPages({ { 0x100800, 0x103400 } }, kPage, 8, r.Fn()), { { 0x100800, 0x103400 } }));
}

static void TestResidencyHandlesSeveralRegions() {
    FakeResidency r;
    r.residentPages = { 0x100, 0x200 };
    const auto kept = KeepResidentPages({ { 0x100000, 0x102000 }, { 0x200000, 0x203000 } }, kPage, 8, r.Fn());
    CHECK(SameRegions(kept, { { 0x100000, 0x101000 }, { 0x200000, 0x201000 } }));
}

static void TestResidencyWithNoQueryTouchesNothing() {
    CHECK(KeepResidentPages({ { 0x100000, 0x110000 } }, kPage, 4, nullptr).empty());
    FakeResidency r;
    CHECK(KeepResidentPages({ { 0x100000, 0x110000 } }, 0, 4, r.Fn()).empty());
    CHECK(KeepResidentPages({ { 0x100000, 0x110000 } }, kPage, 0, r.Fn()).empty());
    CHECK(r.calls == 0);
}

static void TestSliceRegions() {
    const auto slices = SliceRegions({ { 0, 100 }, { 1000, 1050 }, { 2000, 2200 } }, 120);
    // 350 bytes in slices of at most 120
    size_t total = 0;
    for (const auto& s : slices) {
        size_t bytes = 0;
        for (const auto& r : s) bytes += r.end - r.begin;
        CHECK(bytes <= 120);
        total += bytes;
    }
    CHECK(total == 350);
    CHECK(slices.size() == 3);
    CHECK(SameRegions(slices[0], { { 0, 100 }, { 1000, 1020 } }));     // a region split across two slices
    CHECK(SameRegions(slices[1], { { 1020, 1050 }, { 2000, 2090 } }));
    CHECK(SameRegions(slices[2], { { 2090, 2200 } }));
    CHECK(SliceRegions({ { 0, 100 } }, 0).empty());
    CHECK(SliceRegions({}, 100).empty());
    CHECK(SliceRegions({ { 0, 100 } }, 100).size() == 1);              // exactly one full slice, no empty extra
}


// ------------------------------------------------------------------ Vehicle pointers (the "0 / current health" bug)

namespace V = Dinki::Player::Vehicle;

constexpr uintptr_t kCarStartVft = 0x141249810;    // what the start of a car holds
constexpr uintptr_t kCarSecondVft = 0x141249828;   // the vftable of a base-class part, 8 bytes in
static const std::vector<uintptr_t> kCarVfts = { 0x141249810, 0x141249828, 0x141249A38, 0x141249A58 };

// A car like the one in the log: two vftables 8 bytes apart at its start, and health at +0x180/+0x184/+0x188.
static void BuildCar(FakeMemory& m, uintptr_t car, float maxHealth, float curHealth, float lastHealth) {
    m.Add(car, 0x400);
    m.Put64(car + 0x00, kCarStartVft);
    m.Put64(car + 0x08, kCarSecondVft);
    m.PutFloat(car + 0x180, maxHealth);
    m.PutFloat(car + 0x184, curHealth);
    m.PutFloat(car + 0x188, lastHealth);
}

static float ReadFloatAt(const FakeMemory& m, uintptr_t address) {
    float f = 0.0f;
    m.Read(address, &f, sizeof(f));
    return f;
}

static void TestRegressionTwoPointersIntoOneCar() {
    FakeMemory m;
    const uintptr_t car = 0x92653C10;
    BuildCar(m, car, 100.0f, 52.1f, 57.0f);

    const std::vector<V::FieldPointer> pointers = { { 0xB08, car }, { 0x1C40, car + 8 } };   // exactly the log
    const auto groups = V::GroupPointers(m.Reader(), pointers, kCarStartVft, 0x40);

    CHECK(groups.size() == 1);                                   // one car, not two
    CHECK(groups[0].base == car);                                // the START of the object
    CHECK(groups[0].fields.size() == 2 && groups[0].fields[0] == 0xB08 && groups[0].fields[1] == 0x1C40);
    CHECK(groups[0].adjusted);                                   // the second pointer had to be moved back

    // reading health from the resolved start gives (max, current, last)...
    CHECK(ReadFloatAt(m, groups[0].base + 0x180) == 100.0f);
    CHECK(ReadFloatAt(m, groups[0].base + 0x184) == 52.1f);
    // ...whereas reading from the raw second pointer reproduces the bug: "0 / last health"
    CHECK(ReadFloatAt(m, (car + 8) + 0x184) == 0.0f);
    CHECK(ReadFloatAt(m, (car + 8) + 0x180) == 57.0f);
}

static void TestDrivingOnlyTheSecondPointerExists() {
    // while driving only character+0x1C40 was set, so there is no pointer to the start to compare against
    FakeMemory m;
    const uintptr_t car = 0x9335B800;
    BuildCar(m, car, 100.0f, 57.9f, 57.8f);
    const auto groups = V::GroupPointers(m.Reader(), { { 0x1C40, car + 8 } }, kCarStartVft, 0x40);
    CHECK(groups.size() == 1 && groups[0].base == car && groups[0].adjusted);
    CHECK(ReadFloatAt(m, groups[0].base + 0x184) == 57.9f);
}

static void TestPointerAlreadyAtTheStartIsNotMoved() {
    FakeMemory m;
    BuildCar(m, 0x917C74D0, 100.0f, 100.0f, 100.0f);
    bool adjusted = true;
    CHECK(V::ResolveStart(m.Reader(), 0x917C74D0, kCarStartVft, 0x40, adjusted) == 0x917C74D0);
    CHECK(!adjusted);
}

static void TestTwoDifferentCarsStaySeparate() {
    FakeMemory m;
    BuildCar(m, 0x92653C10, 100.0f, 90.0f, 90.0f);
    BuildCar(m, 0x9335B800, 100.0f, 10.0f, 10.0f);
    const auto groups = V::GroupPointers(m.Reader(), { { 0xB08, 0x92653C10 }, { 0x1C40, 0x9335B808 } }, kCarStartVft, 0x40);
    CHECK(groups.size() == 2);
    CHECK(groups[0].base == 0x92653C10 && !groups[0].adjusted);
    CHECK(groups[1].base == 0x9335B800 && groups[1].adjusted);
}

static void TestAdjacentCarsStaySeparate() {
    // two cars close together in memory must still be two vehicles: only pointers into the SAME object merge
    FakeMemory m;
    BuildCar(m, 0x92800000, 100.0f, 80.0f, 80.0f);
    BuildCar(m, 0x92800800, 100.0f, 20.0f, 20.0f);
    const auto groups = V::GroupPointers(m.Reader(), { { 0xB08, 0x92800000 }, { 0x1C40, 0x92800808 } }, kCarStartVft, 0x40);
    CHECK(groups.size() == 2);
    CHECK(groups[0].base == 0x92800000 && groups[1].base == 0x92800800);
    CHECK(ReadFloatAt(m, groups[0].base + 0x184) == 80.0f && ReadFloatAt(m, groups[1].base + 0x184) == 20.0f);
}

static void TestUnresolvablePointerIsLeftAlone() {
    // nothing that looks like a car start within 0x40 bytes back: the pointer is used as it is
    FakeMemory m;
    m.Add(0x92700000, 0x400);
    m.Put64(0x92700000 + 0x100, kCarSecondVft);
    bool adjusted = true;
    CHECK(V::ResolveStart(m.Reader(), 0x92700000 + 0x100, kCarStartVft, 0x40, adjusted) == 0x92700000 + 0x100);
    CHECK(!adjusted);
    // a start further back than the limit is not found either
    FakeMemory far;
    far.Add(0x92710000, 0x400);
    far.Put64(0x92710000, kCarStartVft);
    CHECK(V::ResolveStart(far.Reader(), 0x92710000 + 0x48, kCarStartVft, 0x40, adjusted) == 0x92710000 + 0x48);
    CHECK(V::ResolveStart(far.Reader(), 0x92710000 + 0x40, kCarStartVft, 0x40, adjusted) == 0x92710000);   // exactly at the limit
}

static void TestLearningWhichVftableStartsACar() {
    FakeMemory m;
    const uintptr_t car = 0x92653C10;
    BuildCar(m, car, 100.0f, 52.1f, 57.0f);
    const std::vector<V::FieldPointer> both = { { 0xB08, car }, { 0x1C40, car + 8 } };

    // starting from a wrong guess (the second vftable), the pair of pointers corrects it
    CHECK(V::LearnStartVftable(m.Reader(), both, kCarVfts, kCarSecondVft, 0x40) == kCarStartVft);
    // already right: unchanged
    CHECK(V::LearnStartVftable(m.Reader(), both, kCarVfts, kCarStartVft, 0x40) == kCarStartVft);
    // a single pointer teaches nothing
    CHECK(V::LearnStartVftable(m.Reader(), { { 0x1C40, car + 8 } }, kCarVfts, kCarSecondVft, 0x40) == kCarSecondVft);
    // pointers further apart than the gap are different objects
    FakeMemory two;
    BuildCar(two, 0x92653C10, 100.0f, 1.0f, 1.0f);
    BuildCar(two, 0x92654000, 100.0f, 1.0f, 1.0f);
    CHECK(V::LearnStartVftable(two.Reader(), { { 0xB08, 0x92653C10 }, { 0x1C40, 0x92654000 } }, kCarVfts, kCarSecondVft, 0x40) == kCarSecondVft);
    // a vftable that isn't a vehicle's is never learned
    FakeMemory other;
    other.Add(0x92720000, 0x100);
    other.Put64(0x92720000, 0x14121E460);
    CHECK(V::LearnStartVftable(other.Reader(), { { 0x10, 0x92720000 }, { 0x20, 0x92720008 } }, kCarVfts, kCarStartVft, 0x40) == kCarStartVft);
}

static void TestGroupingWithNothing() {
    FakeMemory m;
    CHECK(V::GroupPointers(m.Reader(), {}, kCarStartVft, 0x40).empty());
    CHECK(V::GroupPointers(nullptr, { { 0x10, 0x92720000 } }, kCarStartVft, 0x40).size() == 1);   // unreadable: kept as is
}

int main() {
    TestClassifyBuild();

    TestHeapObject();
    TestEmbeddedObject();
    TestStaticObject();
    TestWrongVftableIgnored();
    TestPointersIntoTheExeAreNeverFollowed();
    TestJunkValuesAreNotFollowed();
    TestUnreadableHeapPointerIsSkipped();
    TestSeveralSlotsOneObject();
    TestTwoDifferentObjects();
    TestNullReader();

    TestFindsAVector();
    TestBadElementRejectsTheVector();
    TestEmptyVectorIsNotReported();
    TestGarbageIsNotAVector();
    TestInvertedPointersRejected();
    TestLargeVectorEveryElementChecked();
    TestSeveralVectorsSortedByCount();
    TestScanBeyondTheObjectDoesNotCrash();
    TestPredicateCanAcceptARange();

    TestMultiFindsSeveralClassesInOnePass();
    TestMultiFindsBothBasesOfOneObject();
    TestMultiReadsEachPointerOncePerOffset();
    TestMultiNoVftablesOrNoReader();
    TestMultiStaticObjectOfASpecificClass();

    TestRegressionTwoPointersIntoOneCar();
    TestDrivingOnlyTheSecondPointerExists();
    TestPointerAlreadyAtTheStartIsNotMoved();
    TestTwoDifferentCarsStaySeparate();
    TestAdjacentCarsStaySeparate();
    TestUnresolvablePointerIsLeftAlone();
    TestLearningWhichVftableStartsACar();
    TestGroupingWithNothing();

    TestSubtractRange();
    TestFindQwordsAcrossChunks();
    TestFindQwordsLimitsAndGuards();
    TestFindQwordsSkipsUnreadableChunks();
    TestFindQwordsAlignsRegionEdges();
    TestScannerCanExcludeItsOwnBuffer();
    TestFindPointerFields();

    TestInspectPlayerGood();
    TestInspectPlayerBad();
    TestInspectPlayerUsesTheLayout();
    TestHealthLikePairs();
    TestPositionLikeTriples();

    TestAllResident();
    TestNothingResident();
    TestResidentGapsSplitRuns();
    TestRunsJoinAcrossBatches();
    TestResidencyClipsToTheRegion();
    TestResidencyHandlesSeveralRegions();
    TestResidencyWithNoQueryTouchesNothing();
    TestSliceRegions();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
