#pragma once

#include "mmlib.h"

class CVideoManager;

DECLHOOK(void, CVideoManager__Update, (CVideoManager* self, float dt));
