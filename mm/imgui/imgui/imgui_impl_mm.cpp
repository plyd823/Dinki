#include "imgui_impl_mm.h"

//#include "mm.h"
//#include "mm/core/core.h"
//#include "mm/core/graphicsengine/graphicsengine.h"
//
//Graphics::HTexture_t* font_texture = 0;
//
//void ImGui_Impl_MM_Init()
//{
//	ImGuiIO& io = ImGui::GetIO();
//
//	auto& params = NGraphicsEngine::CGraphicsEngine::m_Params;
//
//	io.DisplaySize = ImVec2((float)params.m_Width, (float)params.m_Height);
//	//Log("ImGui | Init %f %f\n", (float)params.m_Width, (float)params.m_Height);
//}
//
//void ImGui_Impl_MM_CreateDeviceObjects()
//{	
//	if (font_texture)
//		return;
//
//	ImGuiIO& io = ImGui::GetIO();
//	unsigned char* pxs;
//	int width, height, bytes_per_pixel;
//	io.Fonts->GetTexDataAsRGBA32(&pxs, &width, &height, &bytes_per_pixel);
//
//	Graphics::SCreate2DTextureParams params;
//	memset(&params, 0, sizeof(Graphics::SCreate2DTextureParams));
//	params.m_Width = width;
//	params.m_Height = height;
//	params.m_NumSlices = 1;
//	params.m_Name = "imgui_font";
//	params.m_AllocAlignment = 0;
//	params.m_DataLayout = Graphics::E_DATA_LAYOUT_COLUMN;
//	params.m_NumMipLevels = 1;
//	params.m_Format = Graphics::SURFACEFORMAT_RGBA32;
//	params.m_MultisampleType = Graphics::MULTISAMPLE_NONE;
//	params.m_UsageType = Graphics::USAGETYPE_DYNAMIC;
//	params.flags &= 0xFFC00000 | 0x20000A;
//
//	auto device = CAvaSingle<NGraphicsEngine::CGraphicsEngine>::Instance->m_GraphicsDevice;
//
//	font_texture = Graphics::Create2DTexture(device, &params);
//
//	uint32_t stride = 0;
//	uint8_t* pixels = (uint8_t*)Graphics::Lock2DTexture(device->m_Context, font_texture, 0, 0, Graphics::LOCKTYPE_WRITE_ONLY, &stride);
//
//	for (int y = 0; y < font_texture->m_Height; y++)
//	{
//		memcpy(pixels, pxs + stride * y, stride);
//		pixels += stride;
//	}
//
//	Graphics::Unlock2DTexture(device->m_Context, font_texture, 0, 0);
//
//	io.Fonts->TexID = (ImTextureID*)font_texture;
//}
//
//#define MAX_VERTEXS 30000
//
//void* g_pVB = nullptr; // vertex buffer
//void* g_pVBEnd = nullptr;
//unsigned int g_VertexBufferSize = 0;
//ImVec4 g_ScissorRect;
//
//
//void ImGui_Impl_MM_RenderDrawData(ImDrawData* draw_data)
//{
//
//}