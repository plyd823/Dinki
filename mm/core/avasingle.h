#pragma once

#define CAvaSingleInstance_EXE(_class, _action) (HookMgr::isSteam ? ((CAvaSingle< _class##_STEAM >::Instance) _action ) : ((CAvaSingle< _class##_AVA >::Instance) _action ) )

template<class T>
class CAvaSingle
{
public:
	static T*& Instance;
};