#pragma once

// Dinki command registry: named commands with rebindable hotkeys, saved to an ini file.
//
// Threading
//   * Tick(), ConsumePress() and IsHeld() are meant for the game thread.
//   * Everything else is safe to call from any thread (the menu uses the render thread).
//
// Typical use in a feature's .cpp:
//
//   static const int kMyCmd = Dinki::RegisterCommand("my.cmd", "Do my thing", "My feature", VK_F6);
//   ...
//   if (Dinki::ConsumePress(kMyCmd)) { ... }   // true once per key press
//   if (Dinki::IsHeld(kMyCmd))       { ... }   // true every frame while the key is down
//
// Hotkeys only fire while the game window has focus, and are paused while the
// Dinki menu is open (except "essential" commands such as the menu's own key).
//
// Settings are named values (on/off switches and sliders) that are saved in the same ini
// file and shown in the menu under their category:
//
//   static const int kShow  = Dinki::RegisterBoolSetting("my.show", "Show my thing", "My feature", true);
//   static const int kScale = Dinki::RegisterSetting("my.scale", "My scale", "My feature", 1.0f, 0.5f, 3.0f);
//   ...
//   if (Dinki::GetSettingBool(kShow)) { ... Dinki::GetSetting(kScale) ... }

#include <cstddef>
#include <functional>
#include <string>

namespace Dinki {

    // Registers a command and returns its handle. Safe to call from static initializers.
    //   id          stable name used in the ini file, e.g. "noclip.toggle"
    //   label       text shown in the menu
    //   category    menu group the command is listed under
    //   defaultKey  Windows virtual-key code, 0 = unbound
    //   essential   cannot be unbound, and keeps working while the menu is open
    //   action      optional callback, run on the game thread when the key is pressed
    // Registering an id that already exists returns the existing handle.
    int RegisterCommand(const char* id, const char* label, const char* category, int defaultKey,
        bool essential = false, std::function<void()> action = nullptr);

    // ---- game thread ----

    // Call once per game frame: loads settings on first use, polls keys, latches presses.
    void Tick();

    // True once for each key press. A press that nobody consumed expires after a few frames.
    bool ConsumePress(int cmd);

    // True while the command's key is held down (game focused, not paused by the menu).
    bool IsHeld(int cmd);

    // How long Tick() has taken, since the last call (the counters then reset).
    struct TickStats {
        unsigned calls = 0;
        double avgMs = 0.0;
        double maxMs = 0.0;
    };
    TickStats TakeTickStats();

    // ---- any thread ----

    struct CommandInfo {
        std::string id;
        std::string label;
        std::string category;
        int key = 0;          // current binding, 0 = unbound
        int defaultKey = 0;
        bool essential = false;
    };

    size_t CommandCount();
    CommandInfo GetCommandInfo(int cmd);

    // Rebinds a command (0 = unbind; ignored for essential commands) and saves the ini.
    void SetKey(int cmd, int vk);
    void ResetAllKeys();

    // True if another command is bound to the same key.
    bool HasConflict(int cmd);

    // The menu is open: only essential commands stay active.
    void SetMenuOpen(bool open);

    // The menu is waiting for a new key: every command is paused.
    void SetRebinding(bool rebinding);

    // ---- settings ----

    struct SettingInfo {
        std::string id;
        std::string label;
        std::string category;
        float value = 0.0f;
        float defaultValue = 0.0f;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        bool isBool = false;   // shown as a checkbox; otherwise a slider from minValue to maxValue
    };

    // Registers a slider setting and returns its handle. Safe to call from static initializers.
    // Registering an id that already exists returns the existing handle.
    int RegisterSetting(const char* id, const char* label, const char* category,
        float defaultValue, float minValue, float maxValue);

    // Registers an on/off setting (stored as 0 or 1).
    int RegisterBoolSetting(const char* id, const char* label, const char* category, bool defaultValue);

    float GetSetting(int setting);                 // 0 for an unknown handle
    bool GetSettingBool(int setting);              // true if the value is 0.5 or more

    // Clamps to the setting's range (bool settings snap to 0 or 1) and ignores NaN/infinity.
    // Pass save = false while dragging a slider, then call SaveSettings() when it is released.
    void SetSetting(int setting, float value, bool save = true);

    size_t SettingCount();
    SettingInfo GetSettingInfo(int setting);
    void ResetAllSettings();

    // Writes the ini file now.
    void SaveSettings();

    // Human-readable key name for a virtual-key code ("Right Shift", "F9", "Unbound").
    std::string VkName(int vk);

    // Full path of the settings file (next to the .asi by default).
    std::string IniPath();

    // Replaceable platform hooks. The defaults use Win32; tests replace them.
    struct Platform {
        std::function<bool(int vk)> keyDown;       // is this virtual key physically down?
        std::function<bool()> gameFocused;         // null = assume the game is focused
        std::function<std::string()> iniPath;      // where to load/save settings
    };
    Platform& GetPlatform();

    // Forget every command and all state, but keep the platform hooks. For unit tests.
    void TestReset();

    // Re-read the ini file now (normally done automatically on the first Tick).
    void ReloadSettings();

} // namespace Dinki
