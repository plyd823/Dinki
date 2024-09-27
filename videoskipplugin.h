#pragma once

#include "plugin.h"

class CVideoManager;

DECLHOOK(void, CVideoManager__Update, (CVideoManager* self, float dt));
