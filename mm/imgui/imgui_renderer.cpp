#include "imgui.h"

void ImGuiRenderer::OnRenderInit(bool reInit)
{
	Mutex().lock();
	auto copy = Renderers();
	Mutex().unlock();

	for (auto& item : copy) {
		item->RenderInit(reInit);
	}
}

void ImGuiRenderer::OnRenderShutdown()
{
	Mutex().lock();
	auto copy = Renderers();
	Mutex().unlock();

	for (auto& item : copy) {
		item->RenderShutdown();
	}
}

void ImGuiRenderer::ProcessGame() {

	Mutex().lock();
	auto copy = Renderers();
	Mutex().unlock();

	for (auto& item : copy) {
		item->ProcessEvents();
		item->Game();
	}
}

void ImGuiRenderer::ProcessRender() {

	Mutex().lock();
	auto copy = Renderers();
	Mutex().unlock();

	for (auto& item : copy) {
		item->ProcessEvents(true);
		item->Render();
	}
}

DEFHOOK(void, UpdatePostSim, (void* ctx)) {
	UpdatePostSim_orig(ctx);
	ImGuiRenderer::ProcessGame();
}

void ImGuiRenderer::Install()
{
	ImGui::DetourDX11::Install(
		ADDRESS(0x140F17A90, 0x142F5EDF0),
		ADDRESS(0x1408788E0, 0x142562700),
		ADDRESS(0x1408790E0, 0x142562F10), 
		OnRenderInit, ProcessRender, OnRenderShutdown);

	HookMgr::Install(ADDRESS(0x1403B0840, 0x141FA5A10), UpdatePostSim_hook, UpdatePostSim_orig);
}
