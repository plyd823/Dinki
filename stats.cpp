/*#include "stats.h"

const char* exported_ext = ".ee"; // Adjust this if necessary

class ImGuiStatsMenu : public ImGuiRenderer {
public:
    ImGuiStatsMenu() : ImGuiRenderer() { // Default Constructor
        show = false;  // Initially, the stats menu is hidden
    }

    void Game() override {
        if (CGameState::m_InMainMenu || CGameState::m_State != CGameState::E_GAME_RUN || IsGuiOccludingMainDraw()) {
            SendRenderEvent([this]() { force_hide = true; }); // Hide menu if the game isn't running or in the main menu
            return;
        }

        SendRenderEvent([this]() { force_hide = false; }); // Show stats menu if the game is running
    }

    void GameHandleEvent(Event const& _event) override {
        if (_event.hash == HASHSTR("focus")) {
            CAvaSingleInstance_EXE(CDeviceManager, ->GetInputManager()->SetInFocus(!_event.Arg<bool>(0))); // Focus input on menu
            CAvaSingle<NGraphicsEngine::CGraphicsEngine>::Instance->SetCursor(_event.Arg<bool>(0) ? 0 : -1); // Set cursor visibility
        }
    }

    // Variables
    bool force_hide = true;
    bool show = false; // Show stats flag

    // Function to display health and speed
    void DisplayStatsWithImGui(void* self) {
        CCharacter* ch = *(CCharacter**)((uintptr_t)self + 0x20); //character object, inherits damageable class
        CVehicle* playerCar = ch->GetVehiclePtr();

        // Display the health (assuming health is a member variable of CCharacter or a related system)
        float health = 0.0f;
        health = ch->GetHealth();  // Or use an appropriate method to access health

        // Start ImGui window
        ImGui::Begin("Player Stats"); // Window title

        // Display the health stat
        ImGui::Text("Health: %.2f", health);

        CVehicle* playerCar = ch->GetVehiclePtr();
        if(playerCar)
        {
            CVector3f velocity = playerCar->GetVelocity();

            // Calculate speed based on the velocity
            float speed = sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
            
            // Display the speed stat
            ImGui::Text("Speed: %.2f", speed);
        }
        else
        {
            CMatrix4f mat;
            ch->GetTransform(&mat);

            CVector3f vecForward = mat.Forward();
            float speed = sqrt(vecForward.x * vecForward.x + vecForward.y * vecForward.y + vecForward.z * vecForward.z);

            // Display the speed stat
            ImGui::Text("Speed: %.2f", speed);
        }

        // End ImGui window
        ImGui::End();
    }

    // Handle key events to toggle stats menu
    void Render() override {
        if (force_hide) // If menu is forced hidden, just return
            return;

        if (ImGui::IsKeyJustDown(ImGuiKey_Slash)) { // Default key bind to toggle stats menu
            show = !show; // Toggle menu visibility
            SendGameEvent(Event("focus").Add(show)); // Send event to update focus state
        }

        if (!show) // If the stats menu is not shown, do nothing
            return;

        // Start ImGui window for the stats menu
        ImGui::Begin("Player Stats Menu");

        // Display the player's stats
        DisplayStatsWithImGui(void* self);

        // End the window
        ImGui::End();
    }
};

// Instantiate the ImGuiStatsMenu
ImGuiStatsMenu statsMenu;
*/