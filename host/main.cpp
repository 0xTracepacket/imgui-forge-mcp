// Live preview host: a Win32 + DirectX11 Dear ImGui window that renders
// spec/ui_spec.json each frame and hot-reloads it when the file changes.
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/backends/imgui_impl_win32.h"
#include "../vendor/imgui/backends/imgui_impl_dx11.h"
#include "UiRenderer.h"
#include "../vendor/json.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <windows.h>

using nlohmann::json;

// ---- D3D state ----
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---- spec file loading + hot reload ----
static std::string   g_specPath = "spec/ui_spec.json";
static json          g_spec;
static std::string   g_loadError;
static FILETIME      g_lastWrite = {};

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

static bool getWriteTime(const std::string& path, FILETIME& ft) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d)) return false;
    ft = d.ftLastWriteTime; return true;
}

// Reload spec if the file changed. Keeps the previous good spec on parse error.
static void maybeReloadSpec(bool force) {
    FILETIME ft;
    if (!getWriteTime(g_specPath, ft)) { if (force) g_loadError = "spec file not found: " + g_specPath; return; }
    if (!force && CompareFileTime(&ft, &g_lastWrite) == 0) return;
    g_lastWrite = ft;
    std::string text;
    if (!readFile(g_specPath, text)) { g_loadError = "cannot read spec file"; return; }
    try {
        g_spec = json::parse(text);
        g_loadError.clear();
    } catch (const std::exception& e) {
        g_loadError = std::string("JSON parse error: ") + e.what();  // keep last good g_spec
    }
}

// On-demand screenshot: anything that touches "<specdir>/.shot" gets a fresh
// "<specdir>/preview.png" written from the rendered backbuffer next frame.
static std::string g_shotReq;
static std::string g_shotOut;

static void initShotPaths() {
    std::string dir = ".";
    size_t s = g_specPath.find_last_of("/\\");
    if (s != std::string::npos) dir = g_specPath.substr(0, s);
    g_shotReq = dir + "/.shot";
    g_shotOut = dir + "/preview.png";
}

static void captureBackbuffer() {
    if (!g_pSwapChain || !g_pd3dDevice) return;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return;
    D3D11_TEXTURE2D_DESC desc; back->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g_pd3dDevice->CreateTexture2D(&sd, nullptr, &staging)) || !staging) { back->Release(); return; }
    g_pd3dDeviceContext->CopyResource(staging, back);
    D3D11_MAPPED_SUBRESOURCE map;
    if (SUCCEEDED(g_pd3dDeviceContext->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        int w = (int)desc.Width, h = (int)desc.Height;
        std::vector<unsigned char> px((size_t)w * h * 4);
        const unsigned char* src = (const unsigned char*)map.pData;
        for (int y = 0; y < h; ++y)
            memcpy(&px[(size_t)y * w * 4], src + (size_t)y * map.RowPitch, (size_t)w * 4);
        g_pd3dDeviceContext->Unmap(staging, 0);
        // swapchain is DXGI_FORMAT_R8G8B8A8_UNORM -> byte order already RGBA
        std::string tmp = g_shotOut + ".tmp";
        if (stbi_write_png(tmp.c_str(), w, h, 4, px.data(), w * 4)) {
            DeleteFileA(g_shotOut.c_str());
            MoveFileA(tmp.c_str(), g_shotOut.c_str()); // atomic-ish for the reader
        }
    }
    staging->Release();
    back->Release();
}

int main(int argc, char** argv) {
    if (argc > 1) g_specPath = argv[1];
    initShotPaths();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr),
                       nullptr, nullptr, nullptr, nullptr, L"ImGuiToolHost", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ImGui Tool - Live Preview",
                                WS_OVERLAPPEDWINDOW, 100, 100, 1100, 800,
                                nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    static ui_render::UiState ui;
    maybeReloadSpec(true);
    DWORD lastPoll = GetTickCount();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10); continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // poll spec file ~4x/sec for hot reload
        DWORD now = GetTickCount();
        if (now - lastPoll > 250) { maybeReloadSpec(false); lastPoll = now; }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // status / control strip
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("imgui-tool")) {
            ImGui::Text("spec: %s", g_specPath.c_str());
            if (g_loadError.empty()) ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "loaded OK");
            else ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%s", g_loadError.c_str());
            if (ImGui::Button("Reload now")) maybeReloadSpec(true);
            ImGui::SameLine();
            ImGui::Text("| %.0f FPS", io.Framerate);
            if (!ui.lastClicked.empty()) { ImGui::SameLine(); ImGui::Text("| clicked: %s", ui.lastClicked.c_str()); }
        }
        ImGui::End();

        ui_render::RenderSpec(g_spec, ui);

        ImGui::Render();
        const float clear[4] = { 0.10f, 0.10f, 0.12f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // fulfill a screenshot request from the rendered backbuffer
        {
            WIN32_FILE_ATTRIBUTE_DATA d;
            if (GetFileAttributesExA(g_shotReq.c_str(), GetFileExInfoStandard, &d)) {
                captureBackbuffer();
                DeleteFileA(g_shotReq.c_str());
            }
        }

        HRESULT hr = g_pSwapChain->Present(1, 0); // vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

// ---- D3D helpers (from the official Dear ImGui win32+dx11 example) ----
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // fall back to WARP software driver
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
