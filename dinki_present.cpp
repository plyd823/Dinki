#include "dinki_present.h"
#include "plugin.h"
#include "mm/imgui/imgui.h"

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

#pragma comment(lib, "d3d11.lib")

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

    using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    constexpr UINT kPresentVtableIndex = 8;   // IUnknown (3) + IDXGIObject (4) + IDXGIDeviceSubObject (1)

    PresentFn g_originalPresent = nullptr;
    bool g_hooked = false;

    // Render thread only (all of this runs inside Present).
    HWND g_window = nullptr;
    ID3D11Device* g_device = nullptr;      // compared by identity only
    bool g_win32Ready = false;
    bool g_dx11Ready = false;
    bool g_renderInitCalled = false;
    WNDPROC g_originalWndProc = nullptr;
    bool g_windowIsUnicode = true;

    std::atomic<bool> g_inputBlocked{ false };
    std::atomic<bool> g_imguiLive{ false };
    std::atomic<unsigned> g_presentThread{ 0 };
    std::atomic<unsigned long long> g_presentCount{ 0 };

    // time spent drawing the overlay, in QueryPerformanceCounter ticks
    std::atomic<unsigned long long> g_costTotal{ 0 };
    std::atomic<unsigned long long> g_costMax{ 0 };
    std::atomic<unsigned> g_costFrames{ 0 };

    // ---- window procedure: let ImGui see everything, optionally hide input from the game ----

    bool IsBlockedMessage(UINT msg) {
        switch (msg) {
        case WM_KEYDOWN: case WM_CHAR: case WM_DEADCHAR: case WM_UNICHAR:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_INPUT:
            return true;
        default:
            // Key and button RELEASES and Alt combinations (WM_SYSKEY*) always get through, so
            // opening the menu can't leave the game thinking a key is stuck, and Alt+Tab keeps working.
            return false;
        }
    }

    LRESULT CALLBACK WndProcHook(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (g_imguiLive.load(std::memory_order_acquire) && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return TRUE;

        if (g_inputBlocked.load(std::memory_order_relaxed) && IsBlockedMessage(msg)) {
            if (msg == WM_INPUT)
                return DefWindowProcW(hwnd, msg, wParam, lParam);   // raw input must reach DefWindowProc for cleanup
            return 0;
        }

        return g_windowIsUnicode ? CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam)
                                 : CallWindowProcA(g_originalWndProc, hwnd, msg, wParam, lParam);
    }

    void SubclassWindow(HWND hwnd) {
        g_windowIsUnicode = IsWindowUnicode(hwnd) != FALSE;
        g_originalWndProc = reinterpret_cast<WNDPROC>(g_windowIsUnicode
            ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcHook))
            : SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcHook)));
        Log("Dinki present: window procedure hooked on 0x%p (%s)", static_cast<void*>(hwnd), g_windowIsUnicode ? "unicode" : "ansi");
    }

    // ---- which swap chain is the game's? ----

    bool LooksLikeGameWindow(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd))
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId())
            return false;
        RECT rect = {};
        GetClientRect(hwnd, &rect);
        return (rect.right - rect.left) >= 320 && (rect.bottom - rect.top) >= 200;
    }

    // ---- ImGui setup ----

    bool EnsureInitialized(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd) {
        if (g_dx11Ready && g_device == device)
            return true;

        const bool deviceChanged = g_dx11Ready;
        if (g_dx11Ready) {
            Log("Dinki present: the game's Direct3D device changed; rebuilding the overlay");
            ImGui_ImplDX11_Shutdown();
            g_dx11Ready = false;
        }

        if (!ImGui::GetCurrentContext()) {
            ImGui::CreateContext();
            ImGui::GetIO().ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
        }
        if (!g_win32Ready) {
            SubclassWindow(hwnd);
            ImGui_ImplWin32_Init(hwnd);
            g_win32Ready = true;
        }
        if (!ImGui_ImplDX11_Init(device, context)) {
            Log("Dinki present: ImGui_ImplDX11_Init failed");
            return false;
        }
        g_dx11Ready = true;
        g_device = device;
        g_imguiLive.store(true, std::memory_order_release);

        if (!g_renderInitCalled) {
            ImGuiRenderer::OnRenderInit(false);
            g_renderInitCalled = true;
            Log("Dinki present: overlay initialized on the game's device");
        }
        else if (deviceChanged) {
            ImGuiRenderer::OnRenderInit(true);
        }
        return true;
    }

    void DrawOverlay(IDXGISwapChain* swapChain) {
        DXGI_SWAP_CHAIN_DESC desc = {};
        if (FAILED(swapChain->GetDesc(&desc)))
            return;

        if (!g_window) {
            if (!LooksLikeGameWindow(desc.OutputWindow))
                return;
            g_window = desc.OutputWindow;
            Log("Dinki present: using the swap chain for window 0x%p (%ux%u)", static_cast<void*>(g_window),
                desc.BufferDesc.Width, desc.BufferDesc.Height);
        }
        else if (desc.OutputWindow != g_window) {
            return;   // some other swap chain (a helper window, another overlay, ...)
        }

        ID3D11Device* device = nullptr;
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || !device)
            return;
        ID3D11DeviceContext* context = nullptr;
        device->GetImmediateContext(&context);
        if (!context) {
            device->Release();
            return;
        }

        // NOTE: the old (address-table) code took the game's own mutex around the overlay draw. That mutex
        // lives in a game structure whose address isn't known for this build, so here the overlay draws on
        // the render thread (the thread that calls Present) without it.

        if (EnsureInitialized(device, context, g_window)) {
            ID3D11Texture2D* backBuffer = nullptr;
            ID3D11RenderTargetView* target = nullptr;
            if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))) && backBuffer) {
                device->CreateRenderTargetView(backBuffer, nullptr, &target);
                backBuffer->Release();
            }

            if (target) {
                // Remember what the game had bound and put it back afterwards.
                ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
                ID3D11DepthStencilView* oldDepth = nullptr;
                context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, &oldDepth);
                context->OMSetRenderTargets(1, &target, nullptr);

                ImGui::GetIO().MouseDrawCursor = g_inputBlocked.load(std::memory_order_relaxed);

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                ImGuiRenderer::ProcessRender();
                ImGui::EndFrame();
                ImGui::Render();
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, oldDepth);
                for (auto* t : oldTargets)
                    if (t)
                        t->Release();
                if (oldDepth)
                    oldDepth->Release();
                target->Release();
            }
        }

        context->Release();
        device->Release();
    }

    HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
        if (!(flags & DXGI_PRESENT_TEST)) {   // occlusion test presents don't show anything
            g_presentThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
            g_presentCount.fetch_add(1, std::memory_order_relaxed);

            LARGE_INTEGER before, after;
            QueryPerformanceCounter(&before);
            DrawOverlay(swapChain);
            QueryPerformanceCounter(&after);

            const unsigned long long cost = static_cast<unsigned long long>(after.QuadPart - before.QuadPart);
            g_costTotal.fetch_add(cost, std::memory_order_relaxed);
            g_costFrames.fetch_add(1, std::memory_order_relaxed);
            unsigned long long seen = g_costMax.load(std::memory_order_relaxed);
            while (cost > seen && !g_costMax.compare_exchange_weak(seen, cost, std::memory_order_relaxed)) {}
        }
        return g_originalPresent(swapChain, syncInterval, flags);
    }

} // namespace

bool Dinki::InstallPresentHook() {
    if (g_hooked)
        return true;

    // A throwaway device and swap chain, only to read the address of Present out of the vtable.
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"DinkiDummyWindow";
    RegisterClassExW(&windowClass);
    HWND dummy = CreateWindowExW(0, windowClass.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 8, 8,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!dummy) {
        Log("Dinki present: could not create the helper window (error %lu)", GetLastError());
        UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 8;
    desc.BufferDesc.Height = 8;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = dummy;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &swapChain, &device, &level, &context);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &swapChain, &device, &level, &context);

    void* present = nullptr;
    if (SUCCEEDED(hr) && swapChain)
        present = (*reinterpret_cast<void***>(swapChain))[kPresentVtableIndex];
    else
        Log("Dinki present: could not create the helper Direct3D device (0x%08lX)", static_cast<unsigned long>(hr));

    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(dummy);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);

    if (!present)
        return false;

    MH_STATUS status = MH_CreateHook(present, reinterpret_cast<LPVOID>(&PresentHook), reinterpret_cast<LPVOID*>(&g_originalPresent));
    if (status != MH_OK) {
        Log("Dinki present: MH_CreateHook failed for Present at 0x%p (MinHook status %d)", present, static_cast<int>(status));
        return false;
    }
    status = MH_EnableHook(present);
    if (status != MH_OK) {
        Log("Dinki present: MH_EnableHook failed for Present at 0x%p (MinHook status %d)", present, static_cast<int>(status));
        return false;
    }

    g_hooked = true;
    Log("Dinki present: hooked IDXGISwapChain::Present at 0x%p", present);
    return true;
}

HWND Dinki::PresentWindow() { return g_window; }

void Dinki::SetInputBlocked(bool blocked) { g_inputBlocked.store(blocked, std::memory_order_relaxed); }
bool Dinki::IsInputBlocked() { return g_inputBlocked.load(std::memory_order_relaxed); }

unsigned Dinki::PresentThreadId() { return g_presentThread.load(std::memory_order_relaxed); }

Dinki::OverlayCost Dinki::TakeOverlayCost() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const double msPerTick = 1000.0 / static_cast<double>(frequency.QuadPart);

    OverlayCost cost;
    const unsigned long long total = g_costTotal.exchange(0, std::memory_order_relaxed);
    const unsigned long long max = g_costMax.exchange(0, std::memory_order_relaxed);
    cost.frames = g_costFrames.exchange(0, std::memory_order_relaxed);
    if (cost.frames) {
        cost.avgMs = static_cast<double>(total) * msPerTick / cost.frames;
        cost.maxMs = static_cast<double>(max) * msPerTick;
    }
    return cost;
}
unsigned long long Dinki::PresentCount() { return g_presentCount.load(std::memory_order_relaxed); }
