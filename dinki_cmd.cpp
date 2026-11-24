#include "dinki_cmd.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

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
namespace {

    // A press nobody consumed is dropped after this many Tick()s, so a tap made while
    // the consuming code wasn't running can't fire much later.
    constexpr unsigned kPendingExpiryFrames = 10;
    constexpr int kMaxVk = 254;

    struct Command {
        std::string id, label, category;
        int key = 0;
        int defaultKey = 0;
        bool essential = false;
        std::function<void()> action;

        bool prevRaw = false;      // raw key state at the previous Tick (ignores focus/menu)
        bool held = false;
        bool pending = false;
        unsigned pendingFrame = 0;
    };

    struct Setting {
        std::string id, label, category;
        float value = 0.0f;
        float defaultValue = 0.0f;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        bool isBool = false;
    };

    struct IniEntry {
        std::string section, id, value;
    };

#ifdef _WIN32
    void IniAnchor() {}

    bool DefaultKeyDown(int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    std::string DefaultIniPath() {
        HMODULE module = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&IniAnchor), &module))
            return "dinki.ini";

        char path[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameA(module, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return "dinki.ini";

        std::string p(path, n);
        const size_t slash = p.find_last_of("\\/");
        const size_t dot = p.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            p.resize(dot);
        return p + ".ini";
    }
#else
    bool DefaultKeyDown(int) { return false; }
    std::string DefaultIniPath() { return "dinki.ini"; }
#endif

    struct State {
        std::mutex mu;       // guards everything below
        std::mutex saveMu;   // serializes file writes
        std::vector<Command> commands;
        std::vector<Setting> settings;
        std::vector<IniEntry> unknown;        // ini entries nothing claimed; kept when saving
        bool loaded = false;
        bool menuOpen = false;
        bool rebinding = false;
        unsigned frame = 0;
        Platform platform;

        double tickCostTotalMs = 0.0;     // how long Tick() has taken (guarded by mu)
        double tickCostMaxMs = 0.0;
        unsigned tickCalls = 0;

        State() {
            platform.keyDown = DefaultKeyDown;
            platform.iniPath = DefaultIniPath;
        }
    };

    State& S() {
        static State s;
        return s;
    }

    bool ValidIndex(const State& s, int cmd) {
        return cmd >= 0 && static_cast<size_t>(cmd) < s.commands.size();
    }

    bool RawKeyDown(const State& s, int vk) {
        return vk > 0 && s.platform.keyDown && s.platform.keyDown(vk);
    }

    // ---- ini ----

    std::string Trim(const std::string& str) {
        size_t b = 0, e = str.size();
        while (b < e && (str[b] == ' ' || str[b] == '\t' || str[b] == '\r' || str[b] == '\n')) ++b;
        while (e > b && (str[e - 1] == ' ' || str[e - 1] == '\t' || str[e - 1] == '\r' || str[e - 1] == '\n')) --e;
        return str.substr(b, e - b);
    }

    // Parses "id=VALUE ; comment" lines into (section, id, value) entries. Blank lines, comments and
    // lines without a usable id/value are skipped. Entries before any [section] count as [keys], which
    // is what hand-written files from the first version of this file format look like.
    std::vector<IniEntry> ParseIni(const std::string& text) {
        std::vector<IniEntry> out;
        std::string section = "keys";
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            if (line[0] == '[') {
                const size_t close = line.find(']');
                if (close != std::string::npos)
                    section = Trim(line.substr(1, close - 1));
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string id = Trim(line.substr(0, eq));
            std::string value = line.substr(eq + 1);
            const size_t comment = value.find(';');
            if (comment != std::string::npos)
                value.resize(comment);
            value = Trim(value);
            if (id.empty() || value.empty())
                continue;
            out.push_back({ section, id, value });
        }
        return out;
    }

    // Decimal or 0x-hex virtual-key code in 0..254.
    bool ParseVk(const std::string& text, int& out) {
        char* end = nullptr;
        const long vk = std::strtol(text.c_str(), &end, 0);
        if (end == text.c_str() || *end != '\0' || vk < 0 || vk > kMaxVk)
            return false;
        out = static_cast<int>(vk);
        return true;
    }

    bool ParseNumber(const std::string& text, float& out) {
        char* end = nullptr;
        const double v = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0' || !std::isfinite(v))
            return false;
        out = static_cast<float>(v);
        return std::isfinite(out);
    }

    float Normalize(const Setting& st, float v) {
        v = std::min(std::max(v, st.minValue), st.maxValue);
        if (st.isBool)
            v = (v >= 0.5f) ? 1.0f : 0.0f;
        return v;
    }

    std::string FormatNumber(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));   // 9 digits round-trips any float
        return buf;
    }

    void AddUnknown(State& s, const IniEntry& e) {
        for (IniEntry& existing : s.unknown)
            if (existing.section == e.section && existing.id == e.id) {
                existing.value = e.value;
                return;
            }
        s.unknown.push_back(e);
    }

    std::string RenderIni(const State& s) {
        std::ostringstream out;
        out << "; Dinki settings. Easiest to change them in the in-game menu (Insert by default).\n";
        out << "; [keys] are Windows virtual-key codes (0 = unbound).\n";

        auto writeUnknown = [&](const std::string& section) {
            for (const IniEntry& e : s.unknown)
                if (e.section == section)
                    out << e.id << '=' << e.value << '\n';
        };

        out << "[keys]\n";
        for (const Command& c : s.commands)
            out << c.id << '=' << c.key << " ; " << c.label << " (" << VkName(c.key) << ")\n";
        writeUnknown("keys");

        bool haveUnknownSettings = false;
        for (const IniEntry& e : s.unknown)
            if (e.section == "settings") { haveUnknownSettings = true; break; }
        if (!s.settings.empty() || haveUnknownSettings) {
            out << "\n[settings]\n";
            for (const Setting& st : s.settings)
                out << st.id << '=' << FormatNumber(st.value) << " ; " << st.label << '\n';
            writeUnknown("settings");
        }

        // Sections this version doesn't know about at all: keep them untouched.
        std::vector<std::string> otherSections;
        for (const IniEntry& e : s.unknown)
            if (e.section != "keys" && e.section != "settings" &&
                std::find(otherSections.begin(), otherSections.end(), e.section) == otherSections.end())
                otherSections.push_back(e.section);
        for (const std::string& section : otherSections) {
            out << "\n[" << section << "]\n";
            writeUnknown(section);
        }
        return out.str();
    }

    Command* FindCommand(State& s, const std::string& id) {
        for (Command& c : s.commands)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    Setting* FindSetting(State& s, const std::string& id) {
        for (Setting& st : s.settings)
            if (st.id == id)
                return &st;
        return nullptr;
    }

    // Returns true if the file existed. Caller holds s.mu.
    bool LoadLocked(State& s) {
        s.loaded = true;
        const std::string path = s.platform.iniPath ? s.platform.iniPath() : std::string();
        if (path.empty())
            return true;
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        std::stringstream buf;
        buf << f.rdbuf();

        s.unknown.clear();
        for (const IniEntry& e : ParseIni(buf.str())) {
            if (e.section == "keys") {
                Command* c = FindCommand(s, e.id);
                if (!c) { AddUnknown(s, e); continue; }
                int vk = 0;
                if (!ParseVk(e.value, vk))
                    continue;
                if (vk == 0 && c->essential)
                    continue;   // essential commands can't be unbound, even by hand-editing
                c->key = vk;
                c->prevRaw = RawKeyDown(s, c->key);
            }
            else if (e.section == "settings") {
                Setting* st = FindSetting(s, e.id);
                if (!st) { AddUnknown(s, e); continue; }
                float v = 0.0f;
                if (!ParseNumber(e.value, v))
                    continue;
                st->value = Normalize(*st, v);
            }
            else {
                AddUnknown(s, e);
            }
        }
        return true;
    }

} // namespace

void SaveSettings() {
    State& s = S();
    std::string text, path;
    {
        std::lock_guard<std::mutex> lock(s.mu);
        text = RenderIni(s);
        path = s.platform.iniPath ? s.platform.iniPath() : std::string();
    }
    if (path.empty())
        return;
    std::lock_guard<std::mutex> saveLock(s.saveMu);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f)
        f << text;
}

int RegisterCommand(const char* id, const char* label, const char* category, int defaultKey,
    bool essential, std::function<void()> action)
{
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);

    for (size_t i = 0; i < s.commands.size(); ++i)
        if (s.commands[i].id == id)
            return static_cast<int>(i);

    Command c;
    c.id = id;
    c.label = label;
    c.category = category;
    c.key = c.defaultKey = (defaultKey >= 0 && defaultKey <= kMaxVk) ? defaultKey : 0;
    c.essential = essential;
    c.action = std::move(action);
    s.commands.push_back(std::move(c));
    return static_cast<int>(s.commands.size() - 1);
}

void Tick() {
    const auto started = std::chrono::steady_clock::now();
    State& s = S();
    std::vector<std::function<void()>> toRun;
    bool createIni = false;
    {
        std::lock_guard<std::mutex> lock(s.mu);
        if (!s.loaded)
            createIni = !LoadLocked(s);   // first run: write a default file so it can be found and edited
        ++s.frame;

        const bool focused = !s.platform.gameFocused || s.platform.gameFocused();

        for (Command& c : s.commands) {
            const bool listening = focused && !s.rebinding && (!s.menuOpen || c.essential);
            const bool raw = RawKeyDown(s, c.key);
            const bool rising = raw && !c.prevRaw;
            c.prevRaw = raw;   // tracked even when not listening, so regaining focus or closing
                               // the menu with a key still down doesn't count as a new press
            c.held = raw && listening;

            if (rising && listening) {
                c.pending = true;
                c.pendingFrame = s.frame;
                if (c.action)
                    toRun.push_back(c.action);
            }
            else if (c.pending && s.frame - c.pendingFrame > kPendingExpiryFrames) {
                c.pending = false;
            }
        }
    }

    if (createIni)
        SaveSettings();
    for (auto& action : toRun)
        action();

    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::lock_guard<std::mutex> lock(s.mu);
    s.tickCostTotalMs += ms;
    s.tickCostMaxMs = std::max(s.tickCostMaxMs, ms);
    ++s.tickCalls;
}

TickStats TakeTickStats() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    TickStats stats;
    stats.calls = s.tickCalls;
    stats.avgMs = s.tickCalls ? s.tickCostTotalMs / s.tickCalls : 0.0;
    stats.maxMs = s.tickCostMaxMs;
    s.tickCostTotalMs = 0.0;
    s.tickCostMaxMs = 0.0;
    s.tickCalls = 0;
    return stats;
}

bool ConsumePress(int cmd) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (!ValidIndex(s, cmd) || !s.commands[cmd].pending)
        return false;
    s.commands[cmd].pending = false;
    return true;
}

bool IsHeld(int cmd) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    return ValidIndex(s, cmd) && s.commands[cmd].held;
}

size_t CommandCount() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.commands.size();
}

CommandInfo GetCommandInfo(int cmd) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    CommandInfo info;
    if (ValidIndex(s, cmd)) {
        const Command& c = s.commands[cmd];
        info.id = c.id;
        info.label = c.label;
        info.category = c.category;
        info.key = c.key;
        info.defaultKey = c.defaultKey;
        info.essential = c.essential;
    }
    return info;
}

void SetKey(int cmd, int vk) {
    State& s = S();
    {
        std::lock_guard<std::mutex> lock(s.mu);
        if (!ValidIndex(s, cmd) || vk < 0 || vk > kMaxVk)
            return;
        Command& c = s.commands[cmd];
        if (vk == 0 && c.essential)
            return;
        c.key = vk;
        c.prevRaw = RawKeyDown(s, vk);   // the key is usually still down from binding it; don't fire
        c.pending = false;
        c.held = false;
    }
    SaveSettings();
}

void ResetAllKeys() {
    State& s = S();
    {
        std::lock_guard<std::mutex> lock(s.mu);
        for (Command& c : s.commands) {
            c.key = c.defaultKey;
            c.prevRaw = RawKeyDown(s, c.key);
            c.pending = false;
            c.held = false;
        }
    }
    SaveSettings();
}

bool HasConflict(int cmd) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (!ValidIndex(s, cmd) || s.commands[cmd].key == 0)
        return false;
    for (size_t i = 0; i < s.commands.size(); ++i)
        if (static_cast<int>(i) != cmd && s.commands[i].key == s.commands[cmd].key)
            return true;
    return false;
}

void SetMenuOpen(bool open) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    s.menuOpen = open;
}

void SetRebinding(bool rebinding) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    s.rebinding = rebinding;
}

namespace {
    int RegisterSettingImpl(const char* id, const char* label, const char* category,
        float defaultValue, float minValue, float maxValue, bool isBool)
    {
        State& s = S();
        std::lock_guard<std::mutex> lock(s.mu);

        for (size_t i = 0; i < s.settings.size(); ++i)
            if (s.settings[i].id == id)
                return static_cast<int>(i);

        Setting st;
        st.id = id;
        st.label = label;
        st.category = category;
        st.minValue = minValue;
        st.maxValue = std::max(minValue, maxValue);
        st.isBool = isBool;
        st.defaultValue = Normalize(st, std::isfinite(defaultValue) ? defaultValue : minValue);
        st.value = st.defaultValue;
        s.settings.push_back(std::move(st));
        return static_cast<int>(s.settings.size() - 1);
    }
} // namespace

int RegisterSetting(const char* id, const char* label, const char* category,
    float defaultValue, float minValue, float maxValue)
{
    return RegisterSettingImpl(id, label, category, defaultValue, minValue, maxValue, false);
}

int RegisterBoolSetting(const char* id, const char* label, const char* category, bool defaultValue) {
    return RegisterSettingImpl(id, label, category, defaultValue ? 1.0f : 0.0f, 0.0f, 1.0f, true);
}

float GetSetting(int setting) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (setting < 0 || static_cast<size_t>(setting) >= s.settings.size())
        return 0.0f;
    return s.settings[setting].value;
}

bool GetSettingBool(int setting) {
    return GetSetting(setting) >= 0.5f;
}

void SetSetting(int setting, float value, bool save) {
    State& s = S();
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(s.mu);
        if (setting < 0 || static_cast<size_t>(setting) >= s.settings.size() || !std::isfinite(value))
            return;
        Setting& st = s.settings[setting];
        const float v = Normalize(st, value);
        changed = (v != st.value);
        st.value = v;
    }
    if (changed && save)
        SaveSettings();
}

size_t SettingCount() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.settings.size();
}

SettingInfo GetSettingInfo(int setting) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    SettingInfo info;
    if (setting >= 0 && static_cast<size_t>(setting) < s.settings.size()) {
        const Setting& st = s.settings[setting];
        info.id = st.id;
        info.label = st.label;
        info.category = st.category;
        info.value = st.value;
        info.defaultValue = st.defaultValue;
        info.minValue = st.minValue;
        info.maxValue = st.maxValue;
        info.isBool = st.isBool;
    }
    return info;
}

void ResetAllSettings() {
    State& s = S();
    {
        std::lock_guard<std::mutex> lock(s.mu);
        for (Setting& st : s.settings)
            st.value = st.defaultValue;
    }
    SaveSettings();
}

std::string VkName(int vk) {
    if (vk <= 0)
        return "Unbound";

    static const struct { int vk; const char* name; } kNames[] = {
        {0x01, "Left Mouse"}, {0x02, "Right Mouse"}, {0x04, "Middle Mouse"}, {0x05, "Mouse 4"}, {0x06, "Mouse 5"},
        {0x08, "Backspace"}, {0x09, "Tab"}, {0x0D, "Enter"}, {0x10, "Shift"}, {0x11, "Ctrl"}, {0x12, "Alt"},
        {0x13, "Pause"}, {0x14, "Caps Lock"}, {0x1B, "Escape"}, {0x20, "Space"},
        {0x21, "Page Up"}, {0x22, "Page Down"}, {0x23, "End"}, {0x24, "Home"},
        {0x25, "Left"}, {0x26, "Up"}, {0x27, "Right"}, {0x28, "Down"},
        {0x2C, "Print Screen"}, {0x2D, "Insert"}, {0x2E, "Delete"},
        {0x5B, "Left Win"}, {0x5C, "Right Win"}, {0x5D, "Menu"},
        {0x6A, "Numpad *"}, {0x6B, "Numpad +"}, {0x6D, "Numpad -"}, {0x6E, "Numpad ."}, {0x6F, "Numpad /"},
        {0x90, "Num Lock"}, {0x91, "Scroll Lock"},
        {0xA0, "Left Shift"}, {0xA1, "Right Shift"}, {0xA2, "Left Ctrl"}, {0xA3, "Right Ctrl"},
        {0xA4, "Left Alt"}, {0xA5, "Right Alt"},
        {0xBA, ";"}, {0xBB, "="}, {0xBC, ","}, {0xBD, "-"}, {0xBE, "."}, {0xBF, "/"}, {0xC0, "`"},
        {0xDB, "["}, {0xDC, "\\"}, {0xDD, "]"}, {0xDE, "'"},
    };
    for (const auto& entry : kNames)
        if (entry.vk == vk)
            return entry.name;

    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= 0x60 && vk <= 0x69) return "Numpad " + std::to_string(vk - 0x60);
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "VK 0x%02X", vk);
    return buf;
}

std::string IniPath() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.platform.iniPath ? s.platform.iniPath() : std::string();
}

Platform& GetPlatform() {
    return S().platform;
}

void TestReset() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    s.commands.clear();
    s.settings.clear();
    s.unknown.clear();
    s.loaded = false;
    s.menuOpen = false;
    s.rebinding = false;
    s.frame = 0;
    s.tickCostTotalMs = 0.0;
    s.tickCostMaxMs = 0.0;
    s.tickCalls = 0;
}

void ReloadSettings() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    LoadLocked(s);
}

} // namespace Dinki
