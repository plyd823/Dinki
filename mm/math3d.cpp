#include "math3d.h"

CVector3f* CVector3f::SpeedLerp(CVector3f& result, CVector3f& from, CVector3f& to, float units, float dt, bool clamp_to_01)
{
	return HookMgr::Call<CVector3f*>(0x1409F0650, &result, &from, &to, units, dt, clamp_to_01);
}
