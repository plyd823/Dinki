#include "plugin.h"

//basic variables
static bool rShiftPressed = false;
static bool enabledNoClip = false;
static float flySpeed = 20.0;
static CVector3f currentPos;

void NoClip(void* self, float deltaTime)
{
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
}