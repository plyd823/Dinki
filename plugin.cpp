#include "plugin.h"
#include "mm/imgui/imgui.h"
#include <mm/game/go/character.h>
#include "mm/game/go/vehicle.h"
#include "mm/game/cameramanager.h"
#include "mm/core/input.h"
#include "videoskipplugin.h"

//DLLMain code, initializes hook manager
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH) {
		HookMgr::Initialize();
		PluginAttach(hModule, dwReason, lpReserved);
	}

	return TRUE;
}

void PluginAttach(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {} //Will run when plugin attached, placeholder for now

//basic variables
bool rShiftPressed = false;
bool enabledNoClip = false;
float flySpeed = 20.0;
CVector3f currentPos;

DEFHOOK(void, CPlayer__UpdateController, (void* self, float deltaTime)) { //hooks into CPlayer::UpdateController(), runs before source code
	CCharacter* ch = *(CCharacter**)((uintptr_t)self + 0x20); //character object, inherits damageable class

    if (GetAsyncKeyState(VK_RSHIFT) & 0x8000) {
        if (!rShiftPressed) { //toggle for right shift being pressed
            rShiftPressed = true;
            enabledNoClip = !enabledNoClip; //toggle no clip mode
            ch->m_Invulnerable = false; //allows damage
            CMatrix4f mat;
            CVehicle* playerCar = ch->GetVehiclePtr();
            if (playerCar) { //is player in a vehicle?
                playerCar->SetVelocity(CVector3f());
                playerCar->GetTransform(&mat);
            }
            else { //is player not in a vehicle?
                ch->ForceNeutralState();
                ch->GetTransform(&mat);  
            }
            currentPos = mat.Position(); //save current frame's position
        }
    }
    else {
        rShiftPressed = false; //finish toggle
    }

    if (enabledNoClip) { //no clip is toggled on

        Input::CInputActionMap* inputMap = CAvaSingleInstance_EXE(CDeviceManager, ->GetInputManager()->GetActionMap("player"));

        flySpeed += inputMap->GetValue("VehicleWeaponSelectLeft") * 60.0f; // MWHEELUP increases fly speed
        flySpeed -= inputMap->GetValue("VehicleWeaponSelectRight") * 60.0f; // MWHEELDOWN decreases fly speed

        //Function Keys will set your speed
        if (GetAsyncKeyState(VK_F1) & 0x8000)
            flySpeed = 10.0f;
        if (GetAsyncKeyState(VK_F2) & 0x8000)
            flySpeed = 100.0f;
        if (GetAsyncKeyState(VK_F3) & 0x8000)
            flySpeed = 200.0f;
        if (GetAsyncKeyState(VK_F4) & 0x8000)
            flySpeed = 300.0f;
        if (GetAsyncKeyState(VK_F5) & 0x8000)
            flySpeed = 400.0f;

        //minimum speed
        if (flySpeed < 10.0f)
            flySpeed = 10.0f;

        CMatrix4f camMat, chMat;
        CQuaternion chQuat;
        CAvaSingle<CCameraControlManager>::Instance->GetCameraMatrix(camMat); //sets camMat to Camera Matrix
        float radX, radY, radZ;
        camMat.ToEuler(radX, radY, radZ); //converts Camera Matrix to radians

        CVehicle* playerCar = ch->GetVehiclePtr(); //if player is not in a vehicle, will return NULL
        CGameObject* playerObj = NULL;
        ch->m_Invulnerable = true; //can't take damage
        if (playerCar) {
            playerObj = playerCar;
            playerCar->SetVelocity(CVector3f());
            chMat = camMat;
            
            chQuat.FromEuler(0, radY, 0); //changes pitch of vehicle based on camera matrix
        }
        else {
            playerObj = ch;
            ch->ForceNeutralState();
            ch->RotateInstantly(radY);
            ch->GetTransform(&chMat);

            chQuat.FromMatrix4(chMat);
        }

        CVector3f chPos = currentPos;
        float normalizedSpeed = flySpeed * deltaTime; //speed will not differ based on fps

        chPos = chPos + (chQuat * CVector3f( //changes x and z position based on WASD keys
            inputMap->GetValue("MoveLeft") ? -normalizedSpeed : inputMap->GetValue("MoveRight") ? normalizedSpeed : 0,
            0.0,
            inputMap->GetValue("MoveForward") ? -normalizedSpeed : inputMap->GetValue("MoveBackward") ? normalizedSpeed : 0));

        chPos.y += (GetAsyncKeyState(VK_SPACE) & 0x8000) ? (normalizedSpeed * 0.5f) :
            (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ? (normalizedSpeed * -0.5f) : 0; //changes y position based on spacebar and left shift

        chMat.SetPosition(chPos); //sets character position using character matrix

        currentPos = chPos; //saves position
        playerObj->SetTransform(&chMat); //sets character transform using player object
    }

    return CPlayer__UpdateController_orig(self, deltaTime); //runs the rest of CPlayer::UpdateController()
}

void PluginHooks() {
    //Spawner
    ImGuiRenderer::Install();
    HookMgr::Patch(ADDRESS(0x1403DB400, 0x141FD4D0C), "\xFF", 1); // veh spawn limit
    HookMgr::Patch(ADDRESS(0x1403DB402, 0x141FD4D0E), "\xFF", 1); // ch spawn limit
    HookMgr::MakeNOP(ADDRESS(0x1405CD1C5, 0x142203AEB), 7); // always Vehicle lockedForPlayer = false

    //No Clip
    INSTALLHOOK(ADDRESS(0x1404C6670, 0x1420DEAC0), CPlayer__UpdateController);

    //Intro Videos Skip
    INSTALLHOOK(ADDRESS(0x1405DE290, 0x142215D90), CVideoManager__Update);
}