#include "plugin.h"
#include "dinki_cmd.h"

//hotkeys (rebindable in the Dinki menu, Insert by default)
static const int kPosSave = Dinki::RegisterCommand("pos.save", "Save position", "Position", VK_F9);
static const int kPosLoad = Dinki::RegisterCommand("pos.load", "Load position (hold to keep loading)", "Position", VK_F10);

static CVector3f currentPos;
static CVector3f savedPos;

static bool hasSaved = false;

void PositionLoader(void* self)
{
    CCharacter* ch = *(CCharacter**)((uintptr_t)self + 0x20);
    CGameObject* playerObj = NULL;

    if (Dinki::ConsumePress(kPosSave))
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
    if (Dinki::IsHeld(kPosLoad))
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
            //testing message sent    NEvent::CSendEvent
        }
    }
}