#include "plugin.h"
#include "dinki_cmd.h"

//hotkeys (rebindable in the Dinki menu, Insert by default)
static const int kNoclipToggle  = Dinki::RegisterCommand("noclip.toggle",  "Toggle noclip",       "Noclip", VK_RSHIFT);
static const int kNoclipSpeed10 = Dinki::RegisterCommand("noclip.speed10", "Fly speed 10",        "Noclip", VK_F1);
static const int kNoclipSpeed100 = Dinki::RegisterCommand("noclip.speed100", "Fly speed 100",     "Noclip", VK_F2);
static const int kNoclipSpeed200 = Dinki::RegisterCommand("noclip.speed200", "Fly speed 200",     "Noclip", VK_F3);
static const int kNoclipSpeed300 = Dinki::RegisterCommand("noclip.speed300", "Fly speed 300",     "Noclip", VK_F4);
static const int kNoclipSpeed400 = Dinki::RegisterCommand("noclip.speed400", "Fly speed 400",     "Noclip", VK_F5);
static const int kNoclipUp      = Dinki::RegisterCommand("noclip.up",      "Fly up (hold)",       "Noclip", VK_SPACE);
static const int kNoclipDown    = Dinki::RegisterCommand("noclip.down",    "Fly down (hold)",     "Noclip", VK_LSHIFT);

//basic variables
static bool enabledNoClip = false;
static float flySpeed = 20.0;
static CVector3f currentPos;

void NoClip(void* self, float deltaTime)
{
    CCharacter* ch = *(CCharacter**)((uintptr_t)self + 0x20); //character object, inherits damageable class

    if (Dinki::ConsumePress(kNoclipToggle)) { //toggle key pressed
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

    if (enabledNoClip) { //no clip is toggled on

        Input::CInputActionMap* inputMap = CAvaSingleInstance_EXE(CDeviceManager, ->GetInputManager()->GetActionMap("player"));

        flySpeed += inputMap->GetValue("VehicleWeaponSelectLeft") * 60.0f; // MWHEELUP increases fly speed
        flySpeed -= inputMap->GetValue("VehicleWeaponSelectRight") * 60.0f; // MWHEELDOWN decreases fly speed

        //Speed hotkeys (F1-F5 by default) set your speed
        if (Dinki::ConsumePress(kNoclipSpeed10))
            flySpeed = 10.0f;
        if (Dinki::ConsumePress(kNoclipSpeed100))
            flySpeed = 100.0f;
        if (Dinki::ConsumePress(kNoclipSpeed200))
            flySpeed = 200.0f;
        if (Dinki::ConsumePress(kNoclipSpeed300))
            flySpeed = 300.0f;
        if (Dinki::ConsumePress(kNoclipSpeed400))
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

        chPos.y += Dinki::IsHeld(kNoclipUp) ? (normalizedSpeed * 0.5f) :
            Dinki::IsHeld(kNoclipDown) ? (normalizedSpeed * -0.5f) : 0; //changes y position based on the fly up/down keys (spacebar and left shift by default)

        chMat.SetPosition(chPos); //sets character position using character matrix

        currentPos = chPos; //saves position
        playerObj->SetTransform(&chMat); //sets character transform using player object
    }
}