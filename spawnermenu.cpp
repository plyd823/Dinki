#include "spawnermenu.h"

const char* exported_ext = ".ee";

class ImGuiSpawnerMenu : public ImGuiRenderer {
public:
	ImGuiSpawnerMenu() : ImGuiRenderer() { //Default Constructor

	}

	void Game() override {
		if (CGameState::m_InMainMenu || CGameState::m_State != CGameState::E_GAME_RUN || IsGuiOccludingMainDraw()) { //if game is main menu or not being ran
			SendRenderEvent([this]() { force_hide = true; }); //hides spawner menu
			return;
		}

		SendRenderEvent([this]() { force_hide = false; }); //if game being ran, will not hide spawner menu
	}

	void GameHandleEvent(Event const& _event) override {
		if (_event.hash == HASHSTR("focus")) { //game focused?
			CAvaSingleInstance_EXE(CDeviceManager, ->GetInputManager()->SetInFocus(!_event.Arg<bool>(0))); //focus input on menu
			CAvaSingle<NGraphicsEngine::CGraphicsEngine>::Instance->SetCursor(_event.Arg<bool>(0) ? 0 : -1); //set cursor on menu
		}
	};

	//variables
	bool force_hide = true;
	bool show = false;

	enum class E_MENU {
		ALL_ENTITY,
		NPC,
		PROPS,
		EMPTY_VEH,
		BUZZARD_FACTION,
		SCROTUS_FACTION,
		ROADKILL_FACTION,
		ENEMY_VEH,
		ALLY_VEH,
		BROKEN_VEH,
		NONE
	};

	E_MENU currentMenu = E_MENU::NONE;

	char input_buffer[256] = { 0 };

	void Render() override {

		if (force_hide) //if it needs to be force hidden, the render function will just never run
			return;

		if (ImGui::IsKeyJustDown(ImGuiKey_Slash)) { //Default Spawner menu key bind
			show = !show; //toggles menu
			memset(input_buffer, 0, sizeof(input_buffer));
			SendGameEvent(Event("focus").Add(show));
		}

		if (!show) //turns off menu if already on when keybind pressed
			return;

		ImGui::Begin("Spawner Menu"); //GUI Window Title

		if (ImGui::Button("All Entities")) //All Entities button
		{
			currentMenu = E_MENU::ALL_ENTITY;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("NPCs")) //NPC button
		{
			currentMenu = E_MENU::NPC;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("Props")) //Props button
		{
			currentMenu = E_MENU::PROPS;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("Empty Vehicles")) //Empty Vehicles button
		{
			currentMenu = E_MENU::EMPTY_VEH;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("Buzzard Faction")) //Buzzard Faction button
		{
			currentMenu = E_MENU::BUZZARD_FACTION;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("Scrotus Faction")) //Scrotus Faction button
		{
			currentMenu = E_MENU::SCROTUS_FACTION;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("Roadkill Faction")) //Roadkill Faction button
		{
			currentMenu = E_MENU::ROADKILL_FACTION;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("Enemy Vehicles")) //Enemy Vehicles button
		{
			currentMenu = E_MENU::ENEMY_VEH;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("Ally Vehicles")) //Ally Vehicles button
		{
			currentMenu = E_MENU::ALLY_VEH;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::SameLine(); //forces buttons on the same lines

		if (ImGui::Button("Broken Vehicles")) //Broken Vehicles button
		{
			currentMenu = E_MENU::BROKEN_VEH;
			memset(input_buffer, 0, sizeof(input_buffer));
		}

		ImGui::InputText("##searcher", input_buffer, sizeof(input_buffer)); //search bar

		ImGui::SameLine(); //forces search bar and search button on same line

		if (ImGui::Button("Spawn")) //if click on spawn button (also creates button at the same time)
		{
			std::string out(input_buffer);
			SendGameEvent([out]() {

				if (!CAvaSingle<CCharacterManager>::Instance->GetPlayerCharacter()) //if player cannot be accessed, spawn will do nothing
					return;

				CHashString spawnId = SpawnSystemGetNewSpawnId(spawnId);
				SSpawnInstanceInfo* newInfo = SSpawnInstanceInfo::New();
				newInfo->m_SpawnId.m_Hash = spawnId.m_Hash;
				newInfo->m_SpawnPatchId.m_Hash = spawnId.m_Hash;
				newInfo->m_DespawnDistanceSq = 90000.0f;
				newInfo->m_SpawnPriority = E_SPAWN_PRIORITY_CRITICAL;
				newInfo->SetEntityFile((out + exported_ext).c_str()); //adds .ee to file path

				CMatrix Mat;
				CQuaternion rot;
				CAvaSingle<CCharacterManager>::Instance->GetPlayerCharacter()->GetTransform(&Mat);
				rot.FromMatrix4(Mat);
				CVector3f pos = Mat.Position() + (rot * CVector3f(0.0f, 0.2f, -8.0f)); // forward
				Mat.SetPosition(pos);

				s_SpawnSystemContext->m_UseSpawnLimits = false;
				SpawnSystemSpawn(newInfo, &Mat);
				SSpawnInstanceInfo::Delete(newInfo);

				});
		}

		ImGui::BeginChild("##Child1", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
		for (const EntityObject& item : EO_LIST)
		{
			bool inThisMenu = false;
			switch (currentMenu) {
				case E_MENU::ALL_ENTITY:
					inThisMenu = true;
					break;

				case E_MENU::NPC:
					inThisMenu = item.npcType == EO_NPC_TYPE::IS_NPC;
					break;

				case E_MENU::PROPS:
					inThisMenu = item.npcType == EO_NPC_TYPE::IS_PROP;
					break;

				case E_MENU::EMPTY_VEH:
					inThisMenu = item.driverType == EO_VEHICLE_DRIVER::IS_EMPTY;
					break;

				case E_MENU::BUZZARD_FACTION:
					inThisMenu = item.facType == EO_FACTION_NAME::BUZZARDS;
					break;

				case E_MENU::SCROTUS_FACTION:
					inThisMenu = item.facType == EO_FACTION_NAME::SCROTUS;
					break;

				case E_MENU::ROADKILL_FACTION:
					inThisMenu = item.facType == EO_FACTION_NAME::ROADKILL;
					break;

				case E_MENU::ENEMY_VEH:
					inThisMenu = item.vehType == EO_VEHICLE_TYPE::ENEMY_VEHICLE;
					break;

				case E_MENU::ALLY_VEH:
					inThisMenu = item.vehType == EO_VEHICLE_TYPE::ALLY_VEHICLE;
					break;

				case E_MENU::BROKEN_VEH:
					inThisMenu = item.vehType == EO_VEHICLE_TYPE::BROKEN_VEHICLE;
					break;

				default:
					break;
			}

			if (!inThisMenu) { //Don't include items not part of this menu
				continue;
			}

			//No search terms were entered
			bool noInput = input_buffer[0] == 0;
			//The search term is part of this item's name
			bool inputInItem = strstr(item.filePath.c_str(), input_buffer) != NULL;

			if (noInput || inputInItem)
			{
				if (ImGui::Selectable(item.filePath.c_str()))
				{
					snprintf(input_buffer, sizeof(input_buffer), "%s", item.filePath.c_str());
					input_buffer[sizeof(input_buffer) - 1] = '\0';
				}
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}
};

ImGuiSpawnerMenu spawnerMenu;