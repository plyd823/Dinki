#include "plugin.h"

static CVector3f currentPos;
static CVector3f savedPos;

static bool hasSaved = false;

void PositionLoader(void* self)
{
    CCharacter* ch = *(CCharacter**)((uintptr_t)self + 0x20);
    CGameObject* playerObj = NULL;

    if (GetAsyncKeyState(VK_F9))
    {
        CMatrix4f mat;

        //Find player or vehicle
        CVehicle* playerCar = ch->GetVehiclePtr();
        if (playerCar)
            playerObj = playerCar;
        else
            playerObj = ch;

        //Get Transform Properties for player or vehicle
        playerObj->GetTransform(&mat);
        
        //save position
        currentPos = mat.Position();
        savedPos = currentPos;

        hasSaved = true;
    }
    if (GetAsyncKeyState(VK_F10))
    {
        CMatrix4f mat;

        //Find player or vehicle
        CVehicle* playerCar = ch->GetVehiclePtr();
        if (playerCar)
            playerObj = playerCar;
        else
            playerObj = ch;

        //Get Transform Properties for player or vehicle
        playerObj->GetTransform(&mat);

        //Check to see if position has been saved before
        if (hasSaved)
        {
            mat.SetPosition(savedPos);
            playerObj->SetTransform(&mat);
        }
    }
}