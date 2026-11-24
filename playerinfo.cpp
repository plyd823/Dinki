#include "playerinfo.h"
#include "dinki_cmd.h"
#include "dinki_build.h"
#include "dinki_gog.h"
#include "dinki_watch.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Watches overlay (the "Live Telemetry" window).
//
// Values are read on the GAME thread, from the CPlayer::UpdateController hook, by Dinki::SampleWatches().
// That produces finished text lines; the renderer below only draws them. Nothing on the render thread
// touches game memory.
//
// Each watch has a checkbox in the Dinki menu. To add one, copy a RegisterWatch call below.

//hotkey and settings (rebindable / adjustable in the Dinki menu, Insert by default)
static const int kOverlayToggle = Dinki::RegisterCommand("watches.toggle", "Show / hide watches", "Watches", VK_F12);
static const int kOverlayShown  = Dinki::RegisterBoolSetting("watches.shown", "Show watches overlay", "Watches", true);
static const int kOverlayScale  = Dinki::RegisterSetting("watches.scale", "Text scale", "Watches", 3.5f, 0.5f, 6.0f);

namespace {

    // Reads memory without risking a crash: a bad address just returns false.
    template <class T>
    bool SafeRead(uintptr_t address, T& out) {
        if (address < 0x10000)
            return false;
#if defined(_MSC_VER)
        // A plain copy that survives an access violation. These run every frame, and ReadProcessMemory is a system
        // call, which can be very slow on a machine with security software.
        __try {
            std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(T));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
#else
        SIZE_T bytesRead = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &out, sizeof(T), &bytesRead)
            && bytesRead == sizeof(T);
#endif
    }

    std::string Fmt(const char* format, ...) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return buffer;
    }

    // ctx is the CPlayer controller passed to CPlayer::UpdateController. The character object
    // (a CCharacter, which inherits CDamageable) is the pointer stored at +0x20. Returns 0 if not available.
    uintptr_t CharacterBase(void* ctx) {
        if (Dinki::IsGog())
            return Dinki::GogPlayerCharacter();   // found and verified at runtime by the GOG core
        uintptr_t character = 0;
        if (!ctx || !SafeRead(reinterpret_cast<uintptr_t>(ctx) + 0x20, character))
            return 0;
        return character;
    }

    using Dinki::WatchStatus;

} // namespace

//---------------------------------------------------------------------------------------------
// Built-in watches. Offsets are from the character object (CCharacter) unless noted.
//---------------------------------------------------------------------------------------------

static const int kWatchStatus = Dinki::RegisterWatch("status", "Loading message", "STATUS", 0xFF0000, true,
    [](void* ctx, std::string& text) {
        if (CharacterBase(ctx) != 0)
            return WatchStatus::Hidden;
        text = Dinki::IsGog() ? "Looking for the player..." : "Loading Character...";
        return WatchStatus::Ok;
    });

static const int kWatchHealth = Dinki::RegisterWatch("health", "Player health", "HEALTH", 0xFF0000, true,
    [](void* ctx, std::string& text) {
        const uintptr_t base = CharacterBase(ctx);
        if (!base)
            return WatchStatus::Hidden;
        float maxHealth = 0.0f, curHealth = 0.0f;
        if (!SafeRead(base + 0x180, maxHealth) || !SafeRead(base + 0x184, curHealth))
            return WatchStatus::Failed;
        text = Fmt("HP: %.1f / %.1f", curHealth, maxHealth);
        return WatchStatus::Ok;
    });

static const int kWatchPosition = Dinki::RegisterWatch("position", "Position", "POSITION", 0x00FF00, true,
    [](void* ctx, std::string& text) {
        const uintptr_t base = CharacterBase(ctx);
        if (!base)
            return WatchStatus::Hidden;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!SafeRead(base + 0x208, x) || !SafeRead(base + 0x20C, y) || !SafeRead(base + 0x210, z))
            return WatchStatus::Failed;
        text = Fmt("X:  %8.3f\nY:  %8.3f\nZ:  %8.3f", x, y, z);
        return WatchStatus::Ok;
    });

static const int kWatchVelocity = Dinki::RegisterWatch("velocity", "Speed and velocity", "VELOCITY", 0x0000FF, true,
    [](void* ctx, std::string& text) {
        const uintptr_t base = CharacterBase(ctx);
        if (!base)
            return WatchStatus::Hidden;
        float speed = 0.0f, vx = 0.0f, vy = 0.0f, vz = 0.0f;
        if (!SafeRead(base + 0x384, speed) || !SafeRead(base + 0x378, vx) ||
            !SafeRead(base + 0x37C, vy) || !SafeRead(base + 0x380, vz))
            return WatchStatus::Failed;
        text = Fmt("Vel: %6.2f\nvX:  %8.3f\nvY:  %8.3f\nvZ:  %8.3f", speed, vx, vy, vz);
        return WatchStatus::Ok;
    });

static const int kWatchVehicle = Dinki::RegisterWatch("vehicle", "Vehicle health", "VEHICLE", 0xFF8000, true,
    [](void* ctx, std::string& text) {
        const uintptr_t base = CharacterBase(ctx);
        if (!base)
            return WatchStatus::Hidden;

        uintptr_t carBase = 0;
        float distanceToPlayer = 0.0f;
        if (Dinki::IsGog()) {
            // the GOG core picks the vehicle nearest the player from the ones the character points at
            if (!Dinki::GogPlayerVehicleInfo(carBase, distanceToPlayer))
                return WatchStatus::Hidden;
        }
        else {
            uintptr_t vtable = 0;
            if (!SafeRead(base, vtable))           // make sure the character is readable before calling into it
                return WatchStatus::Failed;

            CVehicle* car = reinterpret_cast<CCharacter*>(base)->GetVehiclePtr();   // safe: we're on the game thread
            if (!car)
                return WatchStatus::Hidden;        // on foot
            carBase = reinterpret_cast<uintptr_t>(car);
        }

        float maxHealth = 0.0f, curHealth = 0.0f;
        if (!SafeRead(carBase + 0x180, maxHealth) || !SafeRead(carBase + 0x184, curHealth))
            return WatchStatus::Failed;
        text = Fmt("HP: %.1f / %.1f", curHealth, maxHealth);
        if (distanceToPlayer > 15.0f)
            text += Fmt("   (%.0f m away)", distanceToPlayer);
        return WatchStatus::Ok;
    });

static const int kWatchMotionState = Dinki::RegisterWatch("motionstate", "Motion state", "STATE", 0xB3B3B3, false,
    [](void* ctx, std::string& text) {
        const uintptr_t base = CharacterBase(ctx);
        if (!base)
            return WatchStatus::Hidden;
        int8_t state = 0;
        if (!SafeRead(base + 0x17D0, state))
            return WatchStatus::Failed;
        text = Fmt("Motion state: %d", static_cast<int>(state));
        return WatchStatus::Ok;
    });

//---------------------------------------------------------------------------------------------
// Overlay
//---------------------------------------------------------------------------------------------

class DinkiWatchOverlay : public ImGuiRenderer {
public:

    // Game thread, once per frame.
    void Game() override {
        if (Dinki::ConsumePress(kOverlayToggle))
            Dinki::SetSetting(kOverlayShown, Dinki::GetSettingBool(kOverlayShown) ? 0.0f : 1.0f);

        forceHide.store(!Dinki::IsGog() && (CGameState::m_InMainMenu || CGameState::m_State != CGameState::E_GAME_RUN || IsGuiOccludingMainDraw()));
    }

    // Render thread, once per frame. Only draws text that was already sampled.
    void Render() override {
        if (forceHide.load() || !Dinki::GetSettingBool(kOverlayShown))
            return;

        const std::vector<Dinki::WatchLine> lines = Dinki::GetWatchSnapshot();
        if (lines.empty())
            return;

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(20.0f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.0f, 0.5f));
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove;

        ImGui::Begin("Live Telemetry", nullptr, window_flags);
        ImGui::SetWindowFontScale(Dinki::GetSetting(kOverlayScale));

        const std::string* lastGroup = nullptr;
        for (const Dinki::WatchLine& line : lines) {
            if (!lastGroup || *lastGroup != line.group) {
                if (lastGroup)
                    ImGui::Dummy(ImVec2(0.0f, 5.0f));
                const uint32_t rgb = line.groupColorRgb;
                ImGui::TextColored(ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f),
                    "%s", line.group.c_str());
                lastGroup = &line.group;
            }
            ImGui::TextUnformatted(line.text.c_str());
        }

        ImGui::End();
    }

private:
    std::atomic<bool> forceHide{ true };   // written on the game thread, read on the render thread
};

DinkiWatchOverlay watchOverlay;
