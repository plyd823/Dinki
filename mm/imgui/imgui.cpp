#include "imgui.h"

bool ImGui::IsKeyDown(ImGuiKey key)
{
	return IsKeyPressed(ImGui::GetKeyIndex(key), true);
}

bool ImGui::IsKeyJustDown(ImGuiKey key)
{
	return IsKeyPressed(ImGui::GetKeyIndex(key), false);
}