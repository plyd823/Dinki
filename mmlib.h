#pragma once

#include "mm/core/avasingle.h"
#include "mm/game/cameramanager.h"
#include <mm/game/go/character.h>
#include "mm/game/charactermanager.h"
#include "mm/core.h"
#include "mm/game/go/damageable.h"
#include "mm/game/events.h"
#include "mm/game/game.h"
#include "mm/game/go/go.h"
#include "mm/core/graphics/graphicsengine.h"
#include "mm/core/hash.h"
#include "mm/math3d.h"
#include "mm/game/go/pfxgo.h"
#include "mm/game/go/physgo.h"
#include "mm/std.h"
#include "mm/game/go/vehicle.h"
#include "mm/core/input.h"


//Declaration for hook functions
#define DECLHOOK(type, name, args) extern type (*name##_orig)args; type name##_hook args;
//Patch hook function
#define PATCHHOOK(addr, args, size) HookMgr::Patch(addr, args, size)
//Make NOP function
#define MAKENOP(addr, size) HookMgr::MakeNOP(addr, size)
//Install hook function
#define INSTALLHOOK(addr, name) HookMgr::Install(addr, name##_hook, name##_orig)