#include "avasingle.h"
#include "mm/std.h"
#include "mm/hookmgr.h"

#include "mm/core/graphics/graphicsengine.h"
#include "mm/core/input.h"

class CCharacterManager;
class CCameraControlManager;

#define AVASINGLE(gog, steam, inst) inst*& CAvaSingle<inst>::Instance = *(inst**)ADDRESS(gog, steam);

AVASINGLE(0x1417116D0, 0x1417F4EC8, NGraphicsEngine::CGraphicsEngine);
AVASINGLE(0x141711708, 0x0, CDeviceManager_AVA);
AVASINGLE(0x0, 0x1417F4EF0, CDeviceManager_STEAM);
AVASINGLE(0x141711B38, 0x1417F5338, CCharacterManager);
AVASINGLE(0x141711B78, 0x1417F5378, CCameraControlManager);