#include "plugin.h"
#include "mm/imgui/imgui.h"
#include "dinki_cmd.h"
#include "dinki_build.h"
#include "dinki_gog.h"
#include "dinki_present.h"

#include <atomic>
#include <cfloat>
#include <string>
#include <vector>

// The menu's own key. "Essential" = it can't be unbound and still works while the menu is open.
static const int kMenuToggle = Dinki::RegisterCommand("menu.toggle", "Open / close this menu", "Menu", VK_INSERT, true);

// Hotkeys should stay quiet while the game is alt-tabbed.
static bool GameWindowFocused() {
	HWND game = Dinki::IsGog() ? Dinki::PresentWindow() : NGraphicsEngine::CGraphicsEngine::m_Params.m_Hwnd;
	if (!game)
		return false;
	HWND foreground = GetForegroundWindow();
	return foreground == game || foreground == GetAncestor(game, GA_ROOT);
}

static const bool kFocusProviderInstalled = []() {
	Dinki::GetPlatform().gameFocused = GameWindowFocused;
	return true;
}();

class DinkiMenu : public ImGuiRenderer {
public:

	// Game thread, once per frame. Polls hotkeys, then opens/closes the menu.
	void Game() override {
		Dinki::Tick();

		// On the GOG build the old game-state globals aren't known, so the menu is always available there.
		const bool inGame = Dinki::IsGog() || !(CGameState::m_InMainMenu || CGameState::m_State != CGameState::E_GAME_RUN || IsGuiOccludingMainDraw());

		bool wantOpen = open.load();
		if (Dinki::ConsumePress(kMenuToggle) && inGame)
			wantOpen = !wantOpen;
		if (!inGame)
			wantOpen = false;   // never leave the game stuck without input

		if (wantOpen != open.load())
			open.store(wantOpen);

		if (wantOpen != inputGrabbed) {
			inputGrabbed = wantOpen;
			Dinki::SetMenuOpen(wantOpen);
			if (Dinki::IsGog()) {
				Dinki::GogSetInputGrab(wantOpen); // blocks game input and frees the cursor without any game addresses
			}
			else {
				CAvaSingleInstance_EXE(CDeviceManager, ->GetInputManager()->SetInFocus(!wantOpen)); // game stops receiving input
				CAvaSingle<NGraphicsEngine::CGraphicsEngine>::Instance->SetCursor(wantOpen ? 0 : -1); // show/hide the cursor
			}
		}
	}

	// Render thread, once per frame.
	void Render() override {
		if (!open.load()) {
			if (capturing >= 0)
				EndCapture();
			return;
		}

		if (capturing >= 0)
			PollCapture();

		ImGui::SetNextWindowSize(ImVec2(540.0f, 560.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Dinki", nullptr, ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			return;
		}

		ImGui::TextDisabled("Click a key to rebind it.");
		ImGui::TextDisabled("Esc cancels, Backspace or Delete unbinds.");
		ImGui::TextDisabled("Other hotkeys are paused while this menu is open.");
		if (Dinki::IsGog())
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "GOG build: noclip, positions and the spawner are not available yet.");
		ImGui::Separator();

		// Snapshot commands and settings, grouped by category in order of first appearance.
		std::vector<Dinki::CommandInfo> commands;
		std::vector<Dinki::SettingInfo> settings;
		std::vector<std::string> categories;
		auto addCategory = [&categories](const std::string& category) {
			for (const std::string& c : categories)
				if (c == category)
					return;
			categories.push_back(category);
		};
		const int commandCount = static_cast<int>(Dinki::CommandCount());
		for (int i = 0; i < commandCount; ++i) {
			commands.push_back(Dinki::GetCommandInfo(i));
			addCategory(commands.back().category);
		}
		const int settingCount = static_cast<int>(Dinki::SettingCount());
		for (int i = 0; i < settingCount; ++i) {
			settings.push_back(Dinki::GetSettingInfo(i));
			addCategory(settings.back().category);
		}

		for (const std::string& category : categories) {
			if (!ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				continue;

			ImGui::PushID(category.c_str());

			// hotkeys
			bool anyCommands = false;
			for (const Dinki::CommandInfo& info : commands)
				if (info.category == category) { anyCommands = true; break; }

			if (anyCommands && ImGui::BeginTable("keys", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 150.0f);
				ImGui::TableSetupColumn("Clear", ImGuiTableColumnFlags_WidthFixed, 60.0f);

				for (int i = 0; i < commandCount; ++i) {
					const Dinki::CommandInfo& info = commands[i];
					if (info.category != category)
						continue;

					ImGui::TableNextRow();
					ImGui::PushID(i);

					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(info.label.c_str());
					if (Dinki::HasConflict(i)) {
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "(!)");
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Another command is bound to this key.");
					}

					const bool isCapturing = (capturing == i);

					ImGui::TableSetColumnIndex(1);
					const std::string text = (isCapturing ? std::string("Press a key...") : Dinki::VkName(info.key)) + "###bind";
					if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
						if (isCapturing)
							EndCapture();
						else
							BeginCapture(i);
					}

					ImGui::TableSetColumnIndex(2);
					ImGui::BeginDisabled(info.essential || info.key == 0 || isCapturing);
					if (ImGui::Button("Clear"))
						Dinki::SetKey(i, 0);
					ImGui::EndDisabled();

					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			// settings: checkboxes and sliders
			for (int i = 0; i < settingCount; ++i) {
				const Dinki::SettingInfo& info = settings[i];
				if (info.category != category)
					continue;

				ImGui::PushID(1000 + i);
				if (info.isBool) {
					bool on = info.value >= 0.5f;
					if (ImGui::Checkbox(info.label.c_str(), &on))
						Dinki::SetSetting(i, on ? 1.0f : 0.0f);
				}
				else {
					float value = info.value;
					ImGui::SetNextItemWidth(220.0f);
					if (ImGui::SliderFloat(info.label.c_str(), &value, info.minValue, info.maxValue, "%.2f"))
						Dinki::SetSetting(i, value, false);          // applied live, written to disk when released
					if (ImGui::IsItemDeactivatedAfterEdit())
						Dinki::SaveSettings();
				}
				ImGui::PopID();
			}

			ImGui::PopID();
		}

		ImGui::Separator();
		if (ImGui::Button("Reset keys")) {
			if (capturing >= 0)
				EndCapture();
			Dinki::ResetAllKeys();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset settings"))
			Dinki::ResetAllSettings();

		static const std::string iniPath = Dinki::IniPath();
		ImGui::TextDisabled("Saved to: %s", iniPath.c_str());

		ImGui::End();
	}

private:

	// ---- key capture (render thread) ----

	void BeginCapture(int cmd) {
		capturing = cmd;
		for (int vk = 1; vk < 256; ++vk)
			heldAtStart[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
		Dinki::SetRebinding(true);
	}

	void EndCapture() {
		capturing = -1;
		Dinki::SetRebinding(false);
	}

	void PollCapture() {
		for (int vk = 3; vk < 255; ++vk) { // 1 and 2 are the mouse buttons used to click the menu
			if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU)
				continue; // use the left/right variants instead
			const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
			if (!down) {
				heldAtStart[vk] = false;
				continue;
			}
			if (heldAtStart[vk])
				continue; // already held when capture began; wait until it's released

			const int cmd = capturing;
			EndCapture();
			if (vk == VK_ESCAPE)
				return; // cancel
			Dinki::SetKey(cmd, (vk == VK_BACK || vk == VK_DELETE) ? 0 : vk);
			return;
		}
	}

	std::atomic<bool> open{ false }; // written on the game thread, read on the render thread
	bool inputGrabbed = false;       // game thread only
	int capturing = -1;              // render thread only: command being rebound, or -1
	bool heldAtStart[256] = {};      // render thread only
};

DinkiMenu dinkiMenu;
