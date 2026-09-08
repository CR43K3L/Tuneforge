#include "ui/window.hpp"

#include <d3d11.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "common/util.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace tf::ui {
namespace {

constexpr wchar_t kClassName[] = L"TuneforgeWindow";

// Attributs DWM utiles, definis en dur : le SDK ne les expose pas toujours.
constexpr DWORD kDwmUseImmersiveDarkMode  = 20;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmCornerRound = 2;

int frame_thickness_x() {
    return ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
}
int frame_thickness_y() {
    return ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
}

} // namespace

// ===========================================================================
LRESULT CALLBACK Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<Window*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) {
        return 1;
    }

    switch (msg) {
        case WM_NCCALCSIZE:
            // Supprime entierement le rendu de la zone non cliente. Quand la
            // fenetre est agrandie, Windows la deborde de l'epaisseur du cadre :
            // il faut la rentrer manuellement, sinon les bords sont hors ecran.
            if (wp == TRUE) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                if (::IsZoomed(hwnd)) {
                    RECT& rc = params->rgrc[0];
                    const int cx = frame_thickness_x();
                    const int cy = frame_thickness_y();
                    rc.left += cx;
                    rc.right -= cx;
                    rc.top += cy;
                    rc.bottom -= cy;
                }
                return 0;
            }
            break;

        case WM_NCHITTEST: {
            // Bordures de redimensionnement d'abord, barre de titre ensuite.
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT rc{};
            ::GetWindowRect(hwnd, &rc);

            if (!::IsZoomed(hwnd)) {
                const int b = std::max(6, frame_thickness_x());
                const bool left   = pt.x < rc.left + b;
                const bool right  = pt.x >= rc.right - b;
                const bool top    = pt.y < rc.top + b;
                const bool bottom = pt.y >= rc.bottom - b;

                if (top && left)     return HTTOPLEFT;
                if (top && right)    return HTTOPRIGHT;
                if (bottom && left)  return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left)            return HTLEFT;
                if (right)           return HTRIGHT;
                if (top)             return HTTOP;
                if (bottom)          return HTBOTTOM;
            }

            if (drag_allowed_ && pt.y < rc.top + static_cast<int>(titlebar_h_)) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) return 0;
            resize_w_ = LOWORD(lp);
            resize_h_ = HIWORD(lp);
            return 0;

        case WM_DPICHANGED: {
            dpi_scale_ = static_cast<float>(HIWORD(wp)) / 96.0f;
            dpi_changed_ = true;
            const RECT* suggested = reinterpret_cast<RECT*>(lp);
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left,
                           suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = static_cast<LONG>(880 * dpi_scale_);
            mmi->ptMinTrackSize.y = static_cast<LONG>(580 * dpi_scale_);
            return 0;
        }

        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_KEYMENU) return 0;  // pas de menu Alt
            break;

        case WM_CLOSE:
            quit_ = true;
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// ===========================================================================
bool Window::Create(const wchar_t* title, int width, int height) {
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = StaticWndProc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    if (!::RegisterClassExW(&wc)) {
        log_error("RegisterClassEx a echoue : {}", last_error());
        return false;
    }

    dpi_scale_ = 1.0f;
    const int w = static_cast<int>(width * dpi_scale_);
    const int h = static_cast<int>(height * dpi_scale_);

    hwnd_ = ::CreateWindowExW(0, kClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, w, h, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) {
        log_error("CreateWindowEx a echoue : {}", last_error());
        return false;
    }

    dpi_scale_ = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd_);

    // Ombre portee et coins arrondis Windows 11 malgre l'absence de cadre.
    const BOOL dark = TRUE;
    ::DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
    const DWORD corner = kDwmCornerRound;
    ::DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference, &corner, sizeof(corner));
    const MARGINS margins{1, 1, 1, 1};
    ::DwmExtendFrameIntoClientArea(hwnd_, &margins);

    // Force le recalcul de la zone non cliente avec notre WM_NCCALCSIZE.
    ::SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!CreateDeviceD3D()) {
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd_);
        ::UnregisterClassW(kClassName, wc.hInstance);
        return false;
    }

    ::ShowWindow(hwnd_, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd_);

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);
    return true;
}

void Window::Destroy() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    CleanupDeviceD3D();
    if (hwnd_) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    ::UnregisterClassW(kClassName, ::GetModuleHandleW(nullptr));
}

bool Window::PumpEvents() {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) quit_ = true;
    }
    if (quit_) return false;

    // Fenetre reduite ou masquee : on rend la main au systeme.
    if (occluded_ && swapchain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        ::Sleep(16);
        return true;
    }
    occluded_ = false;

    if (resize_w_ != 0 && resize_h_ != 0) {
        CleanupRenderTarget();
        swapchain_->ResizeBuffers(0, resize_w_, resize_h_, DXGI_FORMAT_UNKNOWN, 0);
        resize_w_ = resize_h_ = 0;
        CreateRenderTarget();
    }
    return true;
}

void Window::BeginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Window::EndFrame(const ImVec4& clear_color) {
    ImGui::Render();
    const float clear[4] = {clear_color.x, clear_color.y, clear_color.z, clear_color.w};
    context_->OMSetRenderTargets(1, &rtv_, nullptr);
    context_->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    const HRESULT hr = swapchain_->Present(1, 0);  // synchronisation verticale
    occluded_ = (hr == DXGI_STATUS_OCCLUDED);
}

bool Window::maximized() const { return hwnd_ && ::IsZoomed(hwnd_) != 0; }
bool Window::focused() const { return hwnd_ && ::GetForegroundWindow() == hwnd_; }

void Window::Minimize() { ::ShowWindow(hwnd_, SW_MINIMIZE); }

void Window::ToggleMaximize() {
    ::ShowWindow(hwnd_, ::IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
}

void Window::Close() { quit_ = true; }

bool Window::ConsumeDpiChanged() {
    const bool v = dpi_changed_;
    dpi_changed_ = false;
    return v;
}

// ===========================================================================
bool Window::CreateDeviceD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount       = 2;
    sd.BufferDesc.Width  = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags             = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hwnd_;
    sd.SampleDesc.Count  = 1;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    HRESULT hr = ::D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &sd,
        &swapchain_, &device_, &level, &context_);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        // Pas de GPU compatible (session distante, machine virtuelle) : on
        // bascule sur le rasteriseur logiciel plutot que d'echouer.
        log_warn("GPU Direct3D 11 indisponible : bascule sur WARP");
        hr = ::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                             levels, 2, D3D11_SDK_VERSION, &sd, &swapchain_,
                                             &device_, &level, &context_);
    }
    if (FAILED(hr)) {
        log_error("D3D11CreateDeviceAndSwapChain a echoue : 0x{:08X}",
                  static_cast<unsigned>(hr));
        return false;
    }
    CreateRenderTarget();
    return true;
}

void Window::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (swapchain_) { swapchain_->Release(); swapchain_ = nullptr; }
    if (context_)   { context_->Release();   context_ = nullptr; }
    if (device_)    { device_->Release();    device_ = nullptr; }
}

void Window::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        device_->CreateRenderTargetView(back, nullptr, &rtv_);
        back->Release();
    }
}

void Window::CleanupRenderTarget() {
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
}

} // namespace tf::ui
