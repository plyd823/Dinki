#pragma once

#include <d3d11.h>
#include <xmmintrin.h>
#include "mm/std.h"
#include "mm/math3d.h"

namespace Graphics
{
	class __declspec(align(16))  HContext_t
	{
	public:
		__m128 m_CurrBlendColorF;
		void* m_LastRState;
		void* m_CurrRState;
		__m128 m_VPConstFloat[512];
		__m128 m_FPConstFloat[512];
		__m128 m_CPConstFloat[512];
		__m128 m_GPConstFloat[512];
		ID3D11DeviceContext* m_Context;
		struct HThreadMutexName* m_Mutex;
	};

	class HDevice_t
	{
	public:
		HContext_t* m_Context;
		//Graphics::HTexture_t* m_BackBufferSurface;
		void* m_BackBufferSurface;
		void* m_ActiveRenderSetup;

		IDXGIFactory1* m_DXGIFactory;
		IDXGISwapChain* m_SwapChain;
		ID3D11Device* m_D3DDevice;
		IDXGIOutput** m_DXGIOutputs;

		///big pad///
	};
}

namespace NGraphicsEngine {

	struct SCorner
	{
		unsigned __int8 x;
		unsigned __int8 y;
		unsigned __int8 z;
	};

	class CCamera
	{
	public:
		CVector2f m_OrthoValues;
		int m_OffCenterTiles;
		int m_OffCenterTileX;
		int m_OffCenterTileY;
		CMatrix4f m_PreviousTransformF;
		CMatrix4f m_TransformF;
		CMatrix4f m_TransformT0;
		CMatrix4f m_TransformT1;
		CMatrix4f m_ShakeTransform;
		CMatrix4f m_ProjectionF;
		CMatrix4f m_ViewF;
		CMatrix4f m_ViewProjectionF;
		CMatrix4f m_PreviousProjF;
		CMatrix4f m_PreviousViewF;
		CMatrix4f m_PreviousViewProjectionF;
		CMatrix4f m_View;
		CMatrix4f m_ViewProjection;
		CMatrix4f m_PreviousProj;
		CMatrix4f m_PreviousView;
		CMatrix4f m_PreviousViewProjection;
		CPlane m_FrustumPlane[6];
		CVector4f m_AABNormal[12];
		float m_Distance[6];
		NGraphicsEngine::SCorner m_ClosestCorner[6];
		__int8 m_UseOffCenter : 1;
		__int8 m_ScreenshotSeriesRunning : 1;
		__int8 m_Ortho : 1;
		__int8 m_ComputeView : 1;
		__int8 m_DirtyProjection : 1;
		__int8 m_IsRenderCamera : 1;
		CVector3f m_ConePos;
		CVector3f m_ConeAxis;
		float m_ConeAngleOuterTan;
		float m_ConeAngleOuterCosReci;
		float m_ConeAngleOuterCos;
		float m_ConeAngleOuterTanReci;
		float m_ConeAngleGuardTan;
		float m_ConeAngleGuardCosReci;
		float m_ConeAngleGuardCos;
		float m_FOVT0;
		float m_FOVT1;
		float m_FOV;
		float m_FOVProjFactor;
		float m_FOVFactor;
		float m_Near;
		float m_Far;
		float m_FactorR;
		float m_FactorU;
		float m_AspectRatio;
		bool m_FixAspectRatio;
		int m_Width;
		int m_Height;
		CAABox m_AABB;
	};

	class CGraphicsEngine
	{
	public:

		enum EDisplayModeChangeState : int
		{
			MODECHANGE_UNINITIALIZED = 0x0,
			MODECHANGE_IDLE = 0x1,
			MODECHANGE_APPLYING = 0x2,
		};


		class CGraphicsParams
		{
		public:
			const char* m_AppTitle;
			HWND__* m_Hwnd;
			HICON__* m_Cursors[4];
			int m_FullscreenWidth;
			int m_FullscreenHeight;
			int m_WindowedWidth;
			int m_WindowedHeight;
			bool m_Borderless;
			bool m_Fullscreen;
			unsigned int m_DisplayOutputIndex;
			bool m_HighResShadows;
			unsigned int m_Width;
			unsigned int m_Height;
			bool m_IsHighDef;
			bool m_VSync;
			unsigned int m_RendertargetCount;
			unsigned __int16 m_RefreshRate;
		};

		const struct SResizeCallbackParams
		{
			unsigned int width;
			unsigned int height;
			void* user_data;
		};

		struct SRegisteredResizeParam
		{
			void(__fastcall* m_Callback)(const NGraphicsEngine::CGraphicsEngine::SResizeCallbackParams*);
			void* m_UserData;
		};


		struct HThreadName* m_GraphicsFlipThread;
		//TThread<NGraphicsEngine::CGraphicsEngine> m_UpdateThread;
		char pad[0x68];
		unsigned __int8 m_SyncSpot;
		volatile bool m_CPUFinishedDrawingFence;
		void* m_CPUFinishedDrawingEvent;
		//NGraphicsEngine::CGraphicsEngine::SScreenShotData m_ScreenShotData;
		char pad1[0x130];
		NGraphicsEngine::CGraphicsEngine::EDisplayModeChangeState m_DisplayModeChangeState;
		unsigned int m_TargetWidth;
		unsigned int m_TargetHeight;
		unsigned __int16 m_TargetRefreshRate;
		bool m_TargetFullscreen;
		bool m_TargetVSync;
		bool m_TargetBorderless;
		bool m_Minimized;
		bool m_HasNewValidDisplayMode;
		bool m_AutoChooseDisplayOutput;
		unsigned int m_TargetDisplayOutputIndex;
		unsigned int m_WindowWidth;
		unsigned int m_WindowHeight;
		bool m_SynchronousResize;
		bool m_HasNewWindowSettings;
		bool m_HasNewBorderSettings;
		unsigned int m_WorkerThreads;
		void* m_DrawThreadKickEvent;
		void* m_DrawThreadHandle;
		unsigned int m_DrawThreadID;
		int m_ActiveCursor;
		mm::std::vector<NGraphicsEngine::CGraphicsEngine::SRegisteredResizeParam> m_RegisteredCallbacksVector;
		//aphics::SCreateDeviceParams m_CreateDeviceParams;
		//aphics::SInitializeDeviceParams m_InitializeDeviceParams;
		char pad3[0xA8];
		char pad4[0x40];
		bool m_CreateAndInitializeSuccess;
		void* m_UVAnimationHandler;
		void* m_ShadowManager;
		NGraphicsEngine::CCamera m_RenderCamera;
		//NGraphicsEngine::RenderContext m_RenderContext;
		char pad5[0x1628];
		Graphics::HDevice_t* m_GraphicsDevice;
		/*Graphics::HContext_t* m_GraphicsContext;
		NGraphicsEngine::CGraphicsResourceTracker* m_ResourceTracker;
		Graphics::HTexture_t* m_GBufferTexture[3];
		Graphics::HTexture_t* m_LinearGBufferTexture0;
		Graphics::HTexture_t* m_SRGBGBufferTexture1;
		Graphics::HTexture_t* m_SRGBGBufferTexture2;
		Graphics::HTexture_t* m_GBufferSurface[3];
		Graphics::HTexture_t* m_SRGBGBufferSurface0;
		Graphics::HTexture_t* m_SRGBGBufferSurface1;
		Graphics::HTexture_t* m_SRGBGBufferSurface2;
		Graphics::HRenderSetup_t* m_GBufferRenderSetup;
		Graphics::HRenderSetup_t* m_RenderSetupZ;
		Graphics::HRenderSetup_t* m_RenderSetupZAndVelocity;
		Graphics::HTexture_t* m_VelocityBufferTexture;
		Graphics::HTexture_t* m_VelocityBufferSurface;
		Graphics::HRenderSetup_t* m_MainRenderSetup;
		Graphics::HRenderSetup_t* m_MainRenderSetupZ;
		Graphics::HTexture_t* m_MainDepthTexture;
		Graphics::HTexture_t* m_MainDepthSurface;
		Graphics::HTexture_t* m_MainColorTexture;
		Graphics::HTexture_t* m_MainColorSurface;
		Graphics::HTexture_t* m_MainColorTextureARGB16;
		Graphics::HTexture_t* m_MainColorSurfaceARGB16;
		Graphics::HRenderSetup_t* m_MainRenderSetupARGB16;
		Graphics::HTexture_t* m_BackBufferLinear;
		Graphics::HRenderSetup_t* m_PostDrawRenderSetup;
		Graphics::HRenderSetup_t* m_PostEffectRenderSetup;
		bool m_AutoScaling;
		bool m_AutoScalingGameEnable;
		int m_ScaledWidthMax;
		int m_ScaledHeightMax;
		float m_ScalingTargetms;
		float m_ScalingPosTreshold;
		float m_ScalingAngThreshold;
		int m_ScalingMinwidth;
		int m_ScalingMinheight;
		int m_ScalingWstep;
		int m_ScalingHstep;
		TArray<Graphics::HRenderSetup_t*> m_ScalingExtraRenderSetups;
		int m_ScaledWidth;
		int m_ScaledHeight;
		float m_ScaledFractionalWidth;
		float m_ScaledFractionalHeight;
		bool m_EnableRenderThread;
		float m_OutputGamma;
		bool m_OutputGammaChanged;
		unsigned __int64 m_CPUDrawStallTime;
		unsigned __int64 m_StartTimeWithFlip;
		float m_DrawTime;
		float m_DrawTimeExcludingFlip;
		float m_SPUDrawTime;
		bool m_ToggleMipDebugging;
		Graphics::EMaxAniso m_OldAniso;
		Graphics::EMaxAniso m_Aniso;
		float m_OldTextureBias;
		float m_TextureBias;
		bool m_SoftParticlesEnabled;
		bool m_PointLightSpecularEnabled;
		bool m_PointLightSimple;
		bool m_DoMainDraw;
		unsigned int m_FlipInterval;
		unsigned int m_FlipImmidiateThreshold;
		unsigned int m_ESRAMOffset[4];
		SResourceCache* m_ResourceCache;
		SShaderCache* m_ShaderCache;
		TArray<SResourceHandle*> m_LoadedShaders;
		bool m_ForceMainRenderAlpha;
		bool m_SetForceHDRAlpha;
		bool m_RenderingOffscreen;*/



		static CGraphicsParams& m_Params;

		//CMETHOD(0x140879920, 0x142AA9E20, void, SetCursor(int type), type)
		void SetCursor(int type) {
			if (type != m_ActiveCursor) {
				m_ActiveCursor = type;
				PostMessageA(
					NGraphicsEngine::CGraphicsEngine::m_Params.m_Hwnd,
					0x20u,
					(WPARAM)NGraphicsEngine::CGraphicsEngine::m_Params.m_Hwnd,
					1i64);
			}
		}
	};
}