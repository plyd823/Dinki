#include "playerinfo.h"

class ImGuiPlayerInfoMenu : public ImGuiRenderer {
public:
    ImGuiPlayerInfoMenu() : ImGuiRenderer() { //Default Constructor

    }

    void Game() override {
        if (CGameState::m_InMainMenu || CGameState::m_State != CGameState::E_GAME_RUN || IsGuiOccludingMainDraw()) {
            SendRenderEvent([this]() { force_hide = true; });
            return;
        }

        SendRenderEvent([this]() { force_hide = false; });
    }

    void GameHandleEvent(Event const& _event) override {
        if (_event.hash == HASHSTR("focus")) {
            //CAvaSingleInstance_EXE(CDeviceManager, ->GetInputManager()->SetInFocus(!_event.Arg<bool>(0))); 
            //CAvaSingle<NGraphicsEngine::CGraphicsEngine>::Instance->SetCursor(_event.Arg<bool>(0) ? 0 : -1); 
        }
    };

    bool force_hide = true;
    bool show = true;

    char input_buffer[256] = { 0 };

    void Render() override {
        if (force_hide)
            return;

        if (ImGui::IsKeyJustDown(ImGuiKey_F12)) {
            show = !show;
            memset(input_buffer, 0, sizeof(input_buffer));
            SendGameEvent(Event("focus").Add(show));
        }

        if (!show)
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

        ImGui::Begin("Live Telemetry", &show, window_flags);
        ImGui::SetWindowFontScale(3.5f);

        if (characterVoidPtr != nullptr) {
            if (!IsBadReadPtr((void*)((uintptr_t)characterVoidPtr + 0x20), sizeof(void*))) {
                CCharacter* ch = *(CCharacter**)((uintptr_t)characterVoidPtr + 0x20);

                if (ch != nullptr && !IsBadReadPtr(ch, 0x1800)) {
                    uintptr_t base = (uintptr_t)ch;

                    float maxHealth = *(float*)(base + 0x180);
                    float curHealth = *(float*)(base + 0x184);

                    float posX = *(float*)(base + 0x208);
                    float posY = *(float*)(base + 0x20C);
                    float posZ = *(float*)(base + 0x210);

                    float worldSpeed = *(float*)(base + 0x384);
                    float velX = *(float*)(base + 0x378);
                    float velY = *(float*)(base + 0x37C);
                    float velZ = *(float*)(base + 0x380);

                    int8_t motionState = *(int8_t*)(base + 0x17D0);

                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "HEALTH");
                    ImGui::Text("HP: %.1f / %.1f", curHealth, maxHealth);

                    ImGui::Dummy(ImVec2(0.0f, 5.0f));

                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "POSITION");
                    ImGui::Text("X:  %8.3f", posX);
                    ImGui::Text("Y:  %8.3f", posY);
                    ImGui::Text("Z:  %8.3f", posZ);

                    ImGui::Dummy(ImVec2(0.0f, 5.0f));

                    ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "VELOCITY");
                    ImGui::Text("Vel: %6.2f", worldSpeed);
                    ImGui::Text("vX:  %8.3f", velX);
                    ImGui::Text("vY:  %8.3f", velY);
                    ImGui::Text("vZ:  %8.3f", velZ);

                    ImGui::Dummy(ImVec2(0.0f, 5.0f));

                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.13f, 1.0f), "STATES");
                }
                else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Loading Character...");
                }
            }
            else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Loading Pointer...");
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Waiting for Hook...");
        }

        ImGui::End();
    }
};

ImGuiPlayerInfoMenu playerMenu;