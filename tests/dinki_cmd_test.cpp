// Standalone tests for dinki_cmd.cpp. No game, no Windows needed.
//
//   g++ -std=c++20 -I. tests/dinki_cmd_test.cpp dinki_cmd.cpp -o dinki_cmd_test && ./dinki_cmd_test
//   cl /std:c++20 /EHsc /I. tests\dinki_cmd_test.cpp dinki_cmd.cpp && dinki_cmd_test.exe

#include "dinki_cmd.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
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

// ---- fake platform ----
static bool g_down[256];
static bool g_focused = true;
static std::string g_ini;

static void Release() { for (bool& b : g_down) b = false; }

static void Setup() {
    Dinki::TestReset();
    Release();
    g_focused = true;
    std::error_code ec;
    std::filesystem::remove(g_ini, ec);
    auto& p = Dinki::GetPlatform();
    p.keyDown = [](int vk) { return g_down[vk]; };
    p.gameFocused = [] { return g_focused; };
    p.iniPath = [] { return g_ini; };
}

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void WriteFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

static bool Contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static void TickN(int n) { for (int i = 0; i < n; ++i) Dinki::Tick(); }

enum { KA = 'A', KB = 'B', KC = 'C', KSPACE = 0x20 };

// ------------------------------------------------------------------ tests

static void TestPressIsEdgeTriggered() {
    Setup();
    const int cmd = Dinki::RegisterCommand("t.press", "Press", "Test", KA);

    Dinki::Tick();
    CHECK(!Dinki::ConsumePress(cmd));

    g_down[KA] = true;
    Dinki::Tick();
    CHECK(Dinki::ConsumePress(cmd));     // fires once...
    CHECK(!Dinki::ConsumePress(cmd));    // ...and is consumed

    TickN(5);                            // key still down: no new press
    CHECK(!Dinki::ConsumePress(cmd));

    g_down[KA] = false;
    Dinki::Tick();
    g_down[KA] = true;
    Dinki::Tick();
    CHECK(Dinki::ConsumePress(cmd));     // released and pressed again: new press
}

static void TestHeld() {
    Setup();
    const int cmd = Dinki::RegisterCommand("t.hold", "Hold", "Test", KA);
    Dinki::Tick();
    CHECK(!Dinki::IsHeld(cmd));
    g_down[KA] = true;
    Dinki::Tick();
    CHECK(Dinki::IsHeld(cmd));
    TickN(3);
    CHECK(Dinki::IsHeld(cmd));
    g_down[KA] = false;
    Dinki::Tick();
    CHECK(!Dinki::IsHeld(cmd));
}

static void TestUnconsumedPressExpires() {
    Setup();
    const int soon = Dinki::RegisterCommand("t.soon", "Soon", "Test", KA);
    const int late = Dinki::RegisterCommand("t.late", "Late", "Test", KB);
    g_down[KA] = true;
    g_down[KB] = true;
    Dinki::Tick();
    TickN(5);
    CHECK(Dinki::ConsumePress(soon));    // still available a few frames later
    TickN(20);
    CHECK(!Dinki::ConsumePress(late));   // but not much later
}

static void TestFocusGating() {
    Setup();
    const int cmd = Dinki::RegisterCommand("t.focus", "Focus", "Test", KA);
    Dinki::Tick();

    g_focused = false;
    g_down[KA] = true;                   // pressed while alt-tabbed
    Dinki::Tick();
    CHECK(!Dinki::IsHeld(cmd));
    CHECK(!Dinki::ConsumePress(cmd));

    g_focused = true;                    // come back with the key still down
    Dinki::Tick();
    CHECK(!Dinki::ConsumePress(cmd));    // not a new press
    CHECK(Dinki::IsHeld(cmd));           // but it is held

    g_down[KA] = false;
    Dinki::Tick();
    g_down[KA] = true;
    Dinki::Tick();
    CHECK(Dinki::ConsumePress(cmd));
}

static void TestMenuOpenPausesNonEssential() {
    Setup();
    const int normal = Dinki::RegisterCommand("t.normal", "Normal", "Test", KA);
    const int essential = Dinki::RegisterCommand("t.essential", "Essential", "Test", KB, true);
    Dinki::Tick();

    Dinki::SetMenuOpen(true);
    g_down[KA] = true;
    g_down[KB] = true;
    Dinki::Tick();
    CHECK(!Dinki::ConsumePress(normal));
    CHECK(!Dinki::IsHeld(normal));
    CHECK(Dinki::ConsumePress(essential));   // the menu key still works while the menu is open
    CHECK(Dinki::IsHeld(essential));

    Dinki::SetMenuOpen(false);               // close with A still down
    Dinki::Tick();
    CHECK(!Dinki::ConsumePress(normal));     // no spurious press
    CHECK(Dinki::IsHeld(normal));
}

static void TestRebindingPausesEverything() {
    Setup();
    const int essential = Dinki::RegisterCommand("t.essential", "Essential", "Test", KB, true);
    Dinki::Tick();
    Dinki::SetRebinding(true);
    g_down[KB] = true;
    Dinki::Tick();
    CHECK(!Dinki::ConsumePress(essential));
    CHECK(!Dinki::IsHeld(essential));
    Dinki::SetRebinding(false);
}

static void TestActionCallback() {
    Setup();
    int calls = 0;
    Dinki::RegisterCommand("t.action", "Action", "Test", KA, false, [&calls] { ++calls; });
    Dinki::Tick();
    g_down[KA] = true;
    TickN(4);
    CHECK(calls == 1);                       // once per press, not once per frame
    g_down[KA] = false;
    Dinki::Tick();
    g_down[KA] = true;
    Dinki::Tick();
    CHECK(calls == 2);
}

static void TestRebinding() {
    Setup();
    const int a = Dinki::RegisterCommand("t.a", "A", "Test", KA);
    const int b = Dinki::RegisterCommand("t.b", "B", "Test", KB);
    Dinki::Tick();

    g_down[KC] = true;                       // user just pressed C to bind it
    Dinki::SetKey(a, KC);
    Dinki::Tick();
    CHECK(!Dinki::ConsumePress(a));          // binding must not trigger the command
    CHECK(Dinki::GetCommandInfo(a).key == KC);

    g_down[KC] = false;
    Dinki::Tick();
    g_down[KC] = true;
    Dinki::Tick();
    CHECK(Dinki::ConsumePress(a));           // new key works
    g_down[KC] = false;
    g_down[KA] = true;                       // old key no longer does
    Dinki::Tick();
    CHECK(!Dinki::ConsumePress(a));

    CHECK(!Dinki::HasConflict(a));
    Dinki::SetKey(b, KC);
    CHECK(Dinki::HasConflict(a));            // both on C now
    CHECK(Dinki::HasConflict(b));

    Dinki::SetKey(b, 0);                     // unbind
    CHECK(Dinki::GetCommandInfo(b).key == 0);
    CHECK(!Dinki::HasConflict(a));
}

static void TestEssentialCannotBeUnbound() {
    Setup();
    const int menu = Dinki::RegisterCommand("t.menu", "Menu", "Test", 0x2D, true);
    Dinki::SetKey(menu, 0);
    CHECK(Dinki::GetCommandInfo(menu).key == 0x2D);
    Dinki::SetKey(menu, KC);                 // rebinding it is fine
    CHECK(Dinki::GetCommandInfo(menu).key == KC);
}

static void TestResetAndBadInput() {
    Setup();
    const int a = Dinki::RegisterCommand("t.a", "A", "Test", KA);
    Dinki::SetKey(a, KB);
    Dinki::ResetAllKeys();
    CHECK(Dinki::GetCommandInfo(a).key == KA);
    Dinki::SetKey(a, 999);                   // out of range: ignored
    Dinki::SetKey(a, -4);
    Dinki::SetKey(77, KB);                   // unknown handle: ignored
    CHECK(Dinki::GetCommandInfo(a).key == KA);
    CHECK(!Dinki::ConsumePress(-1));
    CHECK(!Dinki::IsHeld(1234));
    CHECK(Dinki::GetCommandInfo(99).id.empty());
}

static void TestDuplicateRegistrationReturnsSameHandle() {
    Setup();
    const int a1 = Dinki::RegisterCommand("t.same", "First", "Test", KA);
    const int a2 = Dinki::RegisterCommand("t.same", "Second", "Test", KB);
    CHECK(a1 == a2);
    CHECK(Dinki::CommandCount() == 1);
    CHECK(Dinki::GetCommandInfo(a1).label == "First");
}

static void TestIniCreatedOnFirstRun() {
    Setup();
    Dinki::RegisterCommand("t.a", "Alpha", "Test", KA);
    Dinki::RegisterCommand("t.b", "Beta", "Test", 0xA1);
    Dinki::Tick();                           // first Tick loads settings; no file yet -> writes defaults
    const std::string text = ReadFile(g_ini);
    CHECK(Contains(text, "[keys]"));
    CHECK(Contains(text, "t.a=65"));
    CHECK(Contains(text, "t.b=161"));
    CHECK(Contains(text, "Right Shift"));    // human-readable comment
}

static void TestIniRoundTrip() {
    Setup();
    const int a = Dinki::RegisterCommand("t.a", "Alpha", "Test", KA);
    Dinki::RegisterCommand("t.b", "Beta", "Test", KB);
    Dinki::Tick();
    Dinki::SetKey(a, KC);                    // saves immediately
    CHECK(Contains(ReadFile(g_ini), "t.a=67"));

    Setup();                                 // fresh process, but the ini file survives?
    WriteFile(g_ini, "[keys]\nt.a=67 ; Alpha\n");
    const int a2 = Dinki::RegisterCommand("t.a", "Alpha", "Test", KA);
    const int b2 = Dinki::RegisterCommand("t.b", "Beta", "Test", KB);
    Dinki::Tick();
    CHECK(Dinki::GetCommandInfo(a2).key == KC);   // loaded
    CHECK(Dinki::GetCommandInfo(b2).key == KB);   // not in file: keeps its default
}

static void TestIniParsingEdgeCases() {
    Setup();
    WriteFile(g_ini,
        "; comment\n"
        "# another comment\n"
        "[keys]\n"
        "\n"
        "t.hex = 0x41 ; hex value\n"
        "t.dec=66\n"
        "t.bad=notanumber\n"
        "t.big=999\n"
        "t.neg=-5\n"
        "t.empty=\n"
        "no equals sign here\n"
        "t.zero=0\n"
        "t.menu=0\n");
    const int hex = Dinki::RegisterCommand("t.hex", "Hex", "Test", KC);
    const int dec = Dinki::RegisterCommand("t.dec", "Dec", "Test", KC);
    const int bad = Dinki::RegisterCommand("t.bad", "Bad", "Test", KC);
    const int big = Dinki::RegisterCommand("t.big", "Big", "Test", KC);
    const int neg = Dinki::RegisterCommand("t.neg", "Neg", "Test", KC);
    const int empty = Dinki::RegisterCommand("t.empty", "Empty", "Test", KC);
    const int zero = Dinki::RegisterCommand("t.zero", "Zero", "Test", KC);
    const int menu = Dinki::RegisterCommand("t.menu", "Menu", "Test", 0x2D, true);
    Dinki::Tick();
    CHECK(Dinki::GetCommandInfo(hex).key == 0x41);
    CHECK(Dinki::GetCommandInfo(dec).key == 66);
    CHECK(Dinki::GetCommandInfo(bad).key == KC);     // garbage ignored -> default
    CHECK(Dinki::GetCommandInfo(big).key == KC);
    CHECK(Dinki::GetCommandInfo(neg).key == KC);
    CHECK(Dinki::GetCommandInfo(empty).key == KC);
    CHECK(Dinki::GetCommandInfo(zero).key == 0);     // explicit unbind is honored
    CHECK(Dinki::GetCommandInfo(menu).key == 0x2D);  // essential can't be unbound by hand-editing either
}

static void TestUnknownEntriesSurviveSaving() {
    Setup();
    WriteFile(g_ini, "[keys]\nt.a=65\nfuture.feature=88 ; added by a newer version\n");
    const int a = Dinki::RegisterCommand("t.a", "Alpha", "Test", KA);
    Dinki::Tick();
    Dinki::SetKey(a, KB);                    // triggers a save
    const std::string text = ReadFile(g_ini);
    CHECK(Contains(text, "t.a=66"));
    CHECK(Contains(text, "future.feature=88"));
}

static void TestVkNames() {
    CHECK(Dinki::VkName(0) == "Unbound");
    CHECK(Dinki::VkName(0xA1) == "Right Shift");
    CHECK(Dinki::VkName(0xA0) == "Left Shift");
    CHECK(Dinki::VkName(0x20) == "Space");
    CHECK(Dinki::VkName(0x2D) == "Insert");
    CHECK(Dinki::VkName(0x70) == "F1");
    CHECK(Dinki::VkName(0x79) == "F10");
    CHECK(Dinki::VkName('A') == "A");
    CHECK(Dinki::VkName('7') == "7");
    CHECK(Dinki::VkName(0x65) == "Numpad 5");
    CHECK(Dinki::VkName(0xBF) == "/");
    CHECK(Dinki::VkName(0xE8) == "VK 0xE8");
}


// ------------------------------------------------------------------ settings

static void TestSettingDefaultsAndClamping() {
    Setup();
    const int scale = Dinki::RegisterSetting("t.scale", "Scale", "Test", 3.5f, 0.5f, 6.0f);
    CHECK(Dinki::GetSetting(scale) == 3.5f);
    Dinki::SetSetting(scale, 2.0f);
    CHECK(Dinki::GetSetting(scale) == 2.0f);
    Dinki::SetSetting(scale, 99.0f);
    CHECK(Dinki::GetSetting(scale) == 6.0f);           // clamped to max
    Dinki::SetSetting(scale, -5.0f);
    CHECK(Dinki::GetSetting(scale) == 0.5f);           // clamped to min
    Dinki::SetSetting(scale, std::nanf(""));
    CHECK(Dinki::GetSetting(scale) == 0.5f);           // NaN ignored
    Dinki::SetSetting(scale, INFINITY);
    CHECK(Dinki::GetSetting(scale) == 0.5f);           // infinity ignored

    // out-of-range defaults are clamped too
    const int odd = Dinki::RegisterSetting("t.odd", "Odd", "Test", 50.0f, 0.0f, 10.0f);
    CHECK(Dinki::GetSetting(odd) == 10.0f);

    const Dinki::SettingInfo info = Dinki::GetSettingInfo(scale);
    CHECK(info.id == "t.scale" && info.category == "Test" && !info.isBool);
    CHECK(info.defaultValue == 3.5f && info.minValue == 0.5f && info.maxValue == 6.0f);

    CHECK(Dinki::GetSetting(-1) == 0.0f);
    CHECK(Dinki::GetSetting(500) == 0.0f);
    Dinki::SetSetting(500, 1.0f);                       // unknown handle: no crash
    CHECK(Dinki::SettingCount() == 2);
}

static void TestBoolSettings() {
    Setup();
    const int on = Dinki::RegisterBoolSetting("t.on", "On", "Test", true);
    const int off = Dinki::RegisterBoolSetting("t.off", "Off", "Test", false);
    CHECK(Dinki::GetSettingBool(on));
    CHECK(!Dinki::GetSettingBool(off));
    CHECK(Dinki::GetSettingInfo(on).isBool);

    Dinki::SetSetting(on, 0.0f);
    CHECK(!Dinki::GetSettingBool(on));
    Dinki::SetSetting(off, 0.7f);                       // snaps to 1
    CHECK(Dinki::GetSettingBool(off));
    CHECK(Dinki::GetSetting(off) == 1.0f);
    Dinki::SetSetting(off, 0.2f);                       // snaps to 0
    CHECK(Dinki::GetSetting(off) == 0.0f);
}

static void TestDuplicateSettingRegistration() {
    Setup();
    const int a = Dinki::RegisterSetting("t.same", "First", "Test", 1.0f, 0.0f, 2.0f);
    const int b = Dinki::RegisterBoolSetting("t.same", "Second", "Test", true);
    CHECK(a == b);
    CHECK(Dinki::SettingCount() == 1);
    CHECK(Dinki::GetSettingInfo(a).label == "First");
}

static void TestSettingsPersistAndSections() {
    Setup();
    const int scale = Dinki::RegisterSetting("t.scale", "Scale", "Test", 3.5f, 0.5f, 6.0f);
    const int flag = Dinki::RegisterBoolSetting("t.flag", "Flag", "Test", true);
    const int cmd = Dinki::RegisterCommand("t.cmd", "Cmd", "Test", KA);
    Dinki::Tick();                                      // creates the default file
    Dinki::SetSetting(scale, 2.25f);                    // saves
    Dinki::SetSetting(flag, 0.0f);

    const std::string text = ReadFile(g_ini);
    CHECK(Contains(text, "[keys]"));
    CHECK(Contains(text, "[settings]"));
    CHECK(Contains(text, "t.scale=2.25"));
    CHECK(Contains(text, "t.flag=0"));
    CHECK(text.find("[keys]") < text.find("[settings]"));
    CHECK(text.find("t.cmd=65") < text.find("[settings]"));   // keys live under [keys]
    CHECK(text.find("t.scale=") > text.find("[settings]"));    // settings live under [settings]
    (void)cmd;

    // a new process reads the same values back
    Setup();
    WriteFile(g_ini, text);
    const int scale2 = Dinki::RegisterSetting("t.scale", "Scale", "Test", 3.5f, 0.5f, 6.0f);
    const int flag2 = Dinki::RegisterBoolSetting("t.flag", "Flag", "Test", true);
    Dinki::Tick();
    CHECK(Dinki::GetSetting(scale2) == 2.25f);
    CHECK(!Dinki::GetSettingBool(flag2));
}

static void TestFloatRoundTripIsExact() {
    Setup();
    const float values[] = { 0.1f, 1.0f / 3.0f, 3.14159274f, 123456.789f, 1e-7f, 0.0f };
    for (float v : values) {
        Setup();
        const int h = Dinki::RegisterSetting("t.f", "F", "Test", 0.0f, -1e9f, 1e9f);
        Dinki::Tick();
        Dinki::SetSetting(h, v);
        const std::string saved = ReadFile(g_ini);

        Setup();
        WriteFile(g_ini, saved);
        const int h2 = Dinki::RegisterSetting("t.f", "F", "Test", 0.0f, -1e9f, 1e9f);
        Dinki::Tick();
        CHECK(Dinki::GetSetting(h2) == v);               // bit-exact after save + load
    }
}

static void TestSettingsLoadValidation() {
    Setup();
    WriteFile(g_ini,
        "[settings]\n"
        "t.ok=2.5\n"
        "t.high=500\n"
        "t.low=-9\n"
        "t.text=banana\n"
        "t.nan=nan\n"
        "t.inf=inf\n"
        "t.sci=1e1\n"
        "t.flag=0.9\n"
        "t.flag0=0\n");
    const int ok = Dinki::RegisterSetting("t.ok", "", "Test", 1.0f, 0.0f, 10.0f);
    const int high = Dinki::RegisterSetting("t.high", "", "Test", 1.0f, 0.0f, 10.0f);
    const int low = Dinki::RegisterSetting("t.low", "", "Test", 1.0f, 0.0f, 10.0f);
    const int text = Dinki::RegisterSetting("t.text", "", "Test", 4.0f, 0.0f, 10.0f);
    const int nan = Dinki::RegisterSetting("t.nan", "", "Test", 4.0f, 0.0f, 10.0f);
    const int inf = Dinki::RegisterSetting("t.inf", "", "Test", 4.0f, 0.0f, 10.0f);
    const int sci = Dinki::RegisterSetting("t.sci", "", "Test", 4.0f, 0.0f, 100.0f);
    const int flag = Dinki::RegisterBoolSetting("t.flag", "", "Test", false);
    const int flag0 = Dinki::RegisterBoolSetting("t.flag0", "", "Test", true);
    Dinki::Tick();
    CHECK(Dinki::GetSetting(ok) == 2.5f);
    CHECK(Dinki::GetSetting(high) == 10.0f);             // clamped
    CHECK(Dinki::GetSetting(low) == 0.0f);               // clamped
    CHECK(Dinki::GetSetting(text) == 4.0f);              // garbage ignored -> default
    CHECK(Dinki::GetSetting(nan) == 4.0f);
    CHECK(Dinki::GetSetting(inf) == 4.0f);
    CHECK(Dinki::GetSetting(sci) == 10.0f);              // scientific notation accepted
    CHECK(Dinki::GetSettingBool(flag));                   // 0.9 -> on
    CHECK(!Dinki::GetSettingBool(flag0));
}

static void TestSectionsAreSeparate() {
    Setup();
    // a command id under [settings] and a setting id under [keys] must not leak across
    WriteFile(g_ini, "[settings]\nt.cmd=70\n[keys]\nt.scale=65\n");
    const int cmd = Dinki::RegisterCommand("t.cmd", "Cmd", "Test", KA);
    const int scale = Dinki::RegisterSetting("t.scale", "Scale", "Test", 3.0f, 0.0f, 100.0f);
    Dinki::Tick();
    CHECK(Dinki::GetCommandInfo(cmd).key == KA);
    CHECK(Dinki::GetSetting(scale) == 3.0f);
}

static void TestHeaderlessFileCountsAsKeys() {
    Setup();
    WriteFile(g_ini, "t.cmd=66\n");                      // hand-written, no [section]
    const int cmd = Dinki::RegisterCommand("t.cmd", "Cmd", "Test", KA);
    Dinki::Tick();
    CHECK(Dinki::GetCommandInfo(cmd).key == KB);
}

static void TestUnknownSettingsAndSectionsSurvive() {
    Setup();
    WriteFile(g_ini,
        "[keys]\nt.cmd=65\nfuture.key=88\n"
        "[settings]\nt.scale=3\nfuture.opt=1.5 ; from a newer version\n"
        "[extra]\nthing=7\n");
    const int scale = Dinki::RegisterSetting("t.scale", "Scale", "Test", 3.0f, 0.0f, 10.0f);
    Dinki::RegisterCommand("t.cmd", "Cmd", "Test", KA);
    Dinki::Tick();
    Dinki::SetSetting(scale, 4.0f);                       // triggers a save
    const std::string text = ReadFile(g_ini);
    CHECK(Contains(text, "t.scale=4"));
    CHECK(Contains(text, "future.key=88"));
    CHECK(Contains(text, "future.opt=1.5"));
    CHECK(Contains(text, "[extra]"));
    CHECK(Contains(text, "thing=7"));
    CHECK(text.find("future.key=88") < text.find("[settings]"));   // each stays in its own section
    CHECK(text.find("future.opt=1.5") > text.find("[settings]"));
}

static void TestDeferredSaveAndReset() {
    Setup();
    const int h = Dinki::RegisterSetting("t.slider", "Slider", "Test", 1.0f, 0.0f, 5.0f);
    Dinki::Tick();                                        // file created with defaults
    CHECK(Contains(ReadFile(g_ini), "t.slider=1"));

    Dinki::SetSetting(h, 3.0f, false);                    // dragging: memory only
    CHECK(Dinki::GetSetting(h) == 3.0f);
    CHECK(Contains(ReadFile(g_ini), "t.slider=1"));       // file untouched
    Dinki::SaveSettings();                                // released
    CHECK(Contains(ReadFile(g_ini), "t.slider=3"));

    Dinki::ResetAllSettings();
    CHECK(Dinki::GetSetting(h) == 1.0f);
    CHECK(Contains(ReadFile(g_ini), "t.slider=1"));
}


static void TestTickStats() {
    Setup();
    Dinki::RegisterCommand("t.a", "A", "Test", KA);
    CHECK(Dinki::TakeTickStats().calls == 0);
    for (int i = 0; i < 5; ++i)
        Dinki::Tick();
    const auto stats = Dinki::TakeTickStats();
    CHECK(stats.calls == 5);
    CHECK(stats.avgMs >= 0.0 && stats.maxMs >= stats.avgMs);
    CHECK(Dinki::TakeTickStats().calls == 0);            // taking resets

    // a command's action counts toward the cost of the Tick that ran it
    int dummy = 0;
    Dinki::RegisterCommand("t.slow", "Slow", "Test", KB, false, [&dummy] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
        while (std::chrono::steady_clock::now() < end) { ++dummy; }
    });
    Dinki::Tick();
    g_down[KB] = true;
    Dinki::Tick();
    const auto slow = Dinki::TakeTickStats();
    CHECK(slow.calls == 2 && slow.maxMs >= 2.5);
}

int main() {
    g_ini = (std::filesystem::temp_directory_path() / "dinki_cmd_test.ini").string();

    TestPressIsEdgeTriggered();
    TestHeld();
    TestUnconsumedPressExpires();
    TestFocusGating();
    TestMenuOpenPausesNonEssential();
    TestRebindingPausesEverything();
    TestActionCallback();
    TestRebinding();
    TestEssentialCannotBeUnbound();
    TestResetAndBadInput();
    TestDuplicateRegistrationReturnsSameHandle();
    TestIniCreatedOnFirstRun();
    TestIniRoundTrip();
    TestIniParsingEdgeCases();
    TestUnknownEntriesSurviveSaving();
    TestVkNames();
    TestSettingDefaultsAndClamping();
    TestBoolSettings();
    TestDuplicateSettingRegistration();
    TestSettingsPersistAndSections();
    TestFloatRoundTripIsExact();
    TestSettingsLoadValidation();
    TestSectionsAreSeparate();
    TestHeaderlessFileCountsAsKeys();
    TestUnknownSettingsAndSectionsSurvive();
    TestDeferredSaveAndReset();
    TestTickStats();

    std::error_code ec;
    std::filesystem::remove(g_ini, ec);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
