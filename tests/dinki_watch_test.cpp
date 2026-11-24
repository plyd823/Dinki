// Standalone tests for dinki_watch.cpp (links against dinki_cmd.cpp). No game, no Windows needed.
//
//   g++ -std=c++20 -I. tests/dinki_watch_test.cpp dinki_watch.cpp dinki_cmd.cpp -o dinki_watch_test && ./dinki_watch_test
//   cl /std:c++20 /EHsc /I. tests\dinki_watch_test.cpp dinki_watch.cpp dinki_cmd.cpp && dinki_watch_test.exe

#include "dinki_cmd.h"
#include "dinki_watch.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

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

static std::string g_ini;

static void Setup() {
    Dinki::TestReset();
    Dinki::WatchTestReset();
    std::error_code ec;
    std::filesystem::remove(g_ini, ec);
    auto& p = Dinki::GetPlatform();
    p.keyDown = [](int) { return false; };
    p.gameFocused = [] { return true; };
    p.iniPath = [] { return g_ini; };
}

using Dinki::WatchStatus;

static void TestBasicSampling() {
    Setup();
    int sawCtx = 0;
    Dinki::RegisterWatch("a", "Alpha", "GROUP1", 0xFF0000, true,
        [&sawCtx](void* ctx, std::string& text) {
            sawCtx = *static_cast<int*>(ctx);
            text = "alpha-value";
            return WatchStatus::Ok;
        });
    Dinki::RegisterWatch("b", "Beta", "GROUP1", 0xFF0000, true,
        [](void*, std::string& text) { text = "line1\nline2"; return WatchStatus::Ok; });
    Dinki::RegisterWatch("c", "Gamma", "GROUP2", 0x00FF00, true,
        [](void*, std::string& text) { text = "gamma-value"; return WatchStatus::Ok; });

    CHECK(Dinki::GetWatchSnapshot().empty());           // nothing before the first sample

    int ctx = 42;
    Dinki::SampleWatches(&ctx);
    const auto lines = Dinki::GetWatchSnapshot();
    CHECK(sawCtx == 42);                                 // sampler receives the context
    CHECK(lines.size() == 3);
    CHECK(lines[0].label == "Alpha" && lines[0].text == "alpha-value");
    CHECK(lines[0].group == "GROUP1" && lines[0].groupColorRgb == 0xFF0000);
    CHECK(lines[1].text == "line1\nline2");              // multi-line text passes through
    CHECK(lines[2].group == "GROUP2" && lines[2].groupColorRgb == 0x00FF00);   // registration order kept
}

static void TestDisabledWatchIsNotSampled() {
    Setup();
    int calls = 0;
    const int h = Dinki::RegisterWatch("off", "Off", "G", 0xFFFFFF, false,
        [&calls](void*, std::string& text) { ++calls; text = "x"; return WatchStatus::Ok; });
    (void)h;

    Dinki::SampleWatches(nullptr);
    CHECK(calls == 0);                                   // default-off watches never read game memory
    CHECK(Dinki::GetWatchSnapshot().empty());

    // switch it on through its setting, as the menu's checkbox does
    int setting = -1;
    for (size_t i = 0; i < Dinki::SettingCount(); ++i)
        if (Dinki::GetSettingInfo(static_cast<int>(i)).id == "watch.off")
            setting = static_cast<int>(i);
    CHECK(setting >= 0);
    Dinki::SetSetting(setting, 1.0f);
    Dinki::SampleWatches(nullptr);
    CHECK(calls == 1);
    CHECK(Dinki::GetWatchSnapshot().size() == 1);

    Dinki::SetSetting(setting, 0.0f);                    // and off again
    Dinki::SampleWatches(nullptr);
    CHECK(calls == 1);
    CHECK(Dinki::GetWatchSnapshot().empty());            // old lines don't linger
}

static void TestHiddenAndFailed() {
    Setup();
    Dinki::RegisterWatch("hidden", "Hidden", "G", 0xFFFFFF, true,
        [](void*, std::string&) { return WatchStatus::Hidden; });
    Dinki::RegisterWatch("failed", "Failed", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { text = "ignored"; return WatchStatus::Failed; });
    Dinki::RegisterWatch("ok", "Ok", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { text = "fine"; return WatchStatus::Ok; });

    Dinki::SampleWatches(nullptr);
    const auto lines = Dinki::GetWatchSnapshot();
    CHECK(lines.size() == 2);                            // hidden produces no line
    CHECK(lines[0].label == "Failed" && lines[0].text == "?");   // failed shows "?"
    CHECK(lines[1].text == "fine");
}

static void TestThrowingSamplerIsContained() {
    Setup();
    Dinki::RegisterWatch("boom", "Boom", "G", 0xFFFFFF, true,
        [](void*, std::string&) -> WatchStatus { throw std::runtime_error("bad read"); });
    Dinki::RegisterWatch("ok", "Ok", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { text = "fine"; return WatchStatus::Ok; });

    Dinki::SampleWatches(nullptr);
    const auto lines = Dinki::GetWatchSnapshot();
    CHECK(lines.size() == 2);
    CHECK(lines[0].text == "?");                         // one bad watch can't take down the rest
    CHECK(lines[1].text == "fine");
}

static void TestSnapshotIsReplacedEachSample() {
    Setup();
    int n = 0;
    Dinki::RegisterWatch("counter", "Counter", "G", 0xFFFFFF, true,
        [&n](void*, std::string& text) { text = std::to_string(++n); return WatchStatus::Ok; });

    Dinki::SampleWatches(nullptr);
    CHECK(Dinki::GetWatchSnapshot()[0].text == "1");
    Dinki::SampleWatches(nullptr);
    const auto lines = Dinki::GetWatchSnapshot();
    CHECK(lines.size() == 1);
    CHECK(lines[0].text == "2");
}

static void TestDuplicateRegistration() {
    Setup();
    const int a = Dinki::RegisterWatch("dup", "First", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { text = "1"; return WatchStatus::Ok; });
    const int b = Dinki::RegisterWatch("dup", "Second", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { text = "2"; return WatchStatus::Ok; });
    CHECK(a == b);
    Dinki::SampleWatches(nullptr);
    const auto lines = Dinki::GetWatchSnapshot();
    CHECK(lines.size() == 1 && lines[0].text == "1");    // the first registration wins
    CHECK(Dinki::SettingCount() == 1);                   // and there is still one checkbox
}

static void TestWatchSettingsAreInTheMenuAndPersist() {
    Setup();
    Dinki::RegisterWatch("shown", "Shown", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { text = "v"; return WatchStatus::Ok; });
    Dinki::RegisterWatch("hiddenbydefault", "Hidden by default", "G", 0xFFFFFF, false,
        [](void*, std::string& text) { text = "v"; return WatchStatus::Ok; });

    // each watch is a bool setting in the "Watches" category, so the menu lists it automatically
    int found = 0;
    for (size_t i = 0; i < Dinki::SettingCount(); ++i) {
        const auto info = Dinki::GetSettingInfo(static_cast<int>(i));
        if (info.id == "watch.shown" || info.id == "watch.hiddenbydefault") {
            CHECK(info.category == "Watches");
            CHECK(info.isBool);
            ++found;
        }
    }
    CHECK(found == 2);

    // switching one off survives a restart
    Dinki::Tick();
    for (size_t i = 0; i < Dinki::SettingCount(); ++i)
        if (Dinki::GetSettingInfo(static_cast<int>(i)).id == "watch.shown")
            Dinki::SetSetting(static_cast<int>(i), 0.0f);

    Dinki::TestReset();
    Dinki::WatchTestReset();
    Dinki::RegisterWatch("shown", "Shown", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { text = "v"; return WatchStatus::Ok; });
    Dinki::Tick();                                       // loads the ini
    Dinki::SampleWatches(nullptr);
    CHECK(Dinki::GetWatchSnapshot().empty());
}


static void BusyWait(std::chrono::milliseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {}
}

static void TestWatchCostsAreMeasured() {
    Setup();
    Dinki::RegisterWatch("slow", "Slow", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { BusyWait(std::chrono::milliseconds(3)); text = "x"; return WatchStatus::Ok; });
    Dinki::RegisterWatch("fast", "Fast", "G", 0xFFFFFF, true,
        [](void*, std::string& text) { text = "y"; return WatchStatus::Ok; });
    Dinki::RegisterWatch("off", "Off", "G", 0xFFFFFF, false,
        [](void*, std::string& text) { text = "z"; return WatchStatus::Ok; });

    CHECK(Dinki::TakeWatchCosts().empty());              // nothing sampled yet
    for (int i = 0; i < 3; ++i)
        Dinki::SampleWatches(nullptr);

    const auto costs = Dinki::TakeWatchCosts();
    const Dinki::WatchCost* slow = nullptr;
    const Dinki::WatchCost* fast = nullptr;
    bool sawOff = false;
    for (const auto& c : costs) {
        if (c.id == "slow") slow = &c;
        if (c.id == "fast") fast = &c;
        if (c.id == "off") sawOff = true;
    }
    CHECK(slow && fast);
    CHECK(!sawOff);                                      // a switched-off watch is never sampled, so it has no cost
    if (slow && fast) {
        CHECK(slow->samples == 3 && fast->samples == 3);
        CHECK(slow->avgMs >= 2.5 && slow->avgMs < 500.0);    // the 3 ms busy-wait is visible
        CHECK(slow->maxMs >= slow->avgMs);
        CHECK(fast->avgMs < slow->avgMs);
    }
    CHECK(Dinki::TakeWatchCosts().empty());              // taking resets the counters
}

int main() {
    g_ini = (std::filesystem::temp_directory_path() / "dinki_watch_test.ini").string();

    TestBasicSampling();
    TestDisabledWatchIsNotSampled();
    TestHiddenAndFailed();
    TestThrowingSamplerIsContained();
    TestSnapshotIsReplacedEachSample();
    TestDuplicateRegistration();
    TestWatchSettingsAreInTheMenuAndPersist();
    TestWatchCostsAreMeasured();

    std::error_code ec;
    std::filesystem::remove(g_ini, ec);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
