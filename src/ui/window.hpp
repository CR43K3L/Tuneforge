#pragma once
//
// Fenetre sans chrome Windows : la barre de titre est dessinee par nous.
//
// On garde WS_OVERLAPPEDWINDOW pour conserver l'ancrage (Snap), l'animation de
// reduction et la gestion multi-ecran, et on supprime uniquement le rendu de la
// zone non cliente via WM_NCCALCSIZE. L'ombre portee et les coins arrondis de
// Windows 11 sont restaures par DWM.
//
#include <windows.h>

#include "imgui.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

namespace tf::ui {

class Window {
public:
    bool Create(const wchar_t* title, int width, int height);
    void Destroy();

    // false lorsque l'application doit se terminer.
    bool PumpEvents();
    void BeginFrame();
    void EndFrame(const ImVec4& clear_color);

    HWND  hwnd() const { return hwnd_; }
    float dpi_scale() const { return dpi_scale_; }
    bool  maximized() const;
    bool  focused() const;

    void Minimize();
    void ToggleMaximize();
    void Close();

    // Indique a WM_NCHITTEST si le curseur survole une zone de la barre de
    // titre qui doit permettre de deplacer la fenetre. Mis a jour chaque image
    // par le code d'interface.
    void  SetTitlebarHeight(float h) { titlebar_h_ = h; }
    void  SetDragAllowed(bool v) { drag_allowed_ = v; }

    // Vrai une seule fois apres un changement de DPI : le theme et les polices
    // doivent etre recharges.
    bool ConsumeDpiChanged();

private:
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    HWND                     hwnd_ = nullptr;
    ID3D11Device*            device_ = nullptr;
    ID3D11DeviceContext*     context_ = nullptr;
    IDXGISwapChain*          swapchain_ = nullptr;
    ID3D11RenderTargetView*  rtv_ = nullptr;

    UINT  resize_w_ = 0, resize_h_ = 0;
    bool  occluded_ = false;
    bool  quit_ = false;
    bool  dpi_changed_ = false;
    float dpi_scale_ = 1.0f;
    float titlebar_h_ = 40.0f;
    bool  drag_allowed_ = false;
};

} // namespace tf::ui
