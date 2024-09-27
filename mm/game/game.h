#pragma once

#include "mm/mm.h"

class CGameState {
public:

	enum EGameState : __int32
	{
		E_GAME_INSTALL = 0x0,
		E_GAME_INIT = 0x1,
		E_GAME_LOAD = 0x2,
		E_GAME_RUN = 0x3,
		E_GAME_STARTUP = 0x4,
		NOF_GAME_STATES = 0x5,
	};


	static bool& m_InMainMenu;
	static EGameState& m_State;
};

class CGame {
public:

};

inline SMETHODV(0x140211FD0, 0x141DC8130, bool, IsGuiOccludingMainDraw())