#pragma once

#include <d3d11.h>
#include <functional>
#include "../imgui.h"

namespace ImGui
{	
	class DetourDX11
	{
	public:
		typedef long(__stdcall* present)(IDXGISwapChain*, UINT, UINT);
		static present p_present, p_present_target;
		static WNDPROC oWndProc;

		static std::function<void(bool)> _onInit;
		static std::function<void()> _onRender;
		static std::function<void()> _onShutdown;

		static LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		static void Install(uintptr_t flipAddr, uintptr_t createRender, uintptr_t destroyRender, 
			std::function<void(bool)> onInit = nullptr,
			std::function<void()> onRender = nullptr,
			std::function<void()> onShutdown = nullptr);
	};
}
