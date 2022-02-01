// InclusiveCrosshairs.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "InclusiveCrosshairs.h"
#include "trace.h"

#ifdef COMPOSITION
namespace winrt
{
    using namespace winrt::Windows::System;
    using namespace winrt::Windows::UI::Composition;
}

namespace ABI
{
    using namespace ABI::Windows::System;
    using namespace ABI::Windows::UI::Composition::Desktop;
}
#endif

struct InclusiveCrosshairs
{
    bool MyRegisterClass(HINSTANCE hInstance);
    static InclusiveCrosshairs* instance;
    void Terminate();
    void SwitchActivationMode();
    void ApplySettings(InclusiveCrosshairsSettings& settings, bool applyToRuntimeObjects);

private:
    enum class MouseButton
    {
        Left,
        Right
    };

    void DestroyInclusiveCrosshairs();
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void StartDrawing();
    void StopDrawing();
    bool CreateInclusiveCrosshairs();
    void UpdateCrosshairsPosition();
    HHOOK m_mouseHook = NULL;
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) noexcept;

    static constexpr auto m_className = L"MousePointerCrosshairs";
    static constexpr auto m_windowTitle = L"PowerToys Mouse Pointer Crosshairs";
    HWND m_hwndOwner = NULL;
    HWND m_hwnd = NULL;
    HINSTANCE m_hinstance = NULL;
    static constexpr DWORD WM_SWITCH_ACTIVATION_MODE = WM_APP;

    winrt::DispatcherQueueController m_dispatcherQueueController{ nullptr };
    winrt::Compositor m_compositor{ nullptr };
    winrt::Desktop::DesktopWindowTarget m_target{ nullptr };
    winrt::ContainerVisual m_root{ nullptr };
    winrt::LayerVisual m_crosshairs_border_layer{ nullptr };
    winrt::LayerVisual m_crosshairs_layer{ nullptr };
    winrt::SpriteVisual m_left_crosshairs_border{ nullptr };
    winrt::SpriteVisual m_left_crosshairs{ nullptr };
    winrt::SpriteVisual m_right_crosshairs_border{ nullptr };
    winrt::SpriteVisual m_right_crosshairs{ nullptr };
    winrt::SpriteVisual m_top_crosshairs_border{ nullptr };
    winrt::SpriteVisual m_top_crosshairs{ nullptr };
    winrt::SpriteVisual m_bottom_crosshairs_border{ nullptr };
    winrt::SpriteVisual m_bottom_crosshairs{ nullptr };

    bool m_visible = false;
    bool m_destroyed = false;

    // Configurable Settings
    winrt::Windows::UI::Color m_crosshairs_border_color = INCLUSIVE_MOUSE_DEFAULT_CROSSHAIRS_BORDER_COLOR;
    winrt::Windows::UI::Color m_crosshairs_color = INCLUSIVE_MOUSE_DEFAULT_CROSSHAIRS_COLOR;
    float m_crosshairs_radius = INCLUSIVE_MOUSE_DEFAULT_CROSSHAIRS_RADIUS;
    float m_crosshairs_thickness = INCLUSIVE_MOUSE_DEFAULT_CROSSHAIRS_THICKNESS;
    float m_crosshairs_border_size = INCLUSIVE_MOUSE_DEFAULT_CROSSHAIRS_BORDER_SIZE;
    float m_crosshairs_opacity = max(0.f, min(1.f, (float)INCLUSIVE_MOUSE_DEFAULT_CROSSHAIRS_OPACITY / 100.0f));
};

InclusiveCrosshairs* InclusiveCrosshairs::instance = nullptr;

bool InclusiveCrosshairs::CreateInclusiveCrosshairs()
{
    try
    {
        // We need a dispatcher queue.
        DispatcherQueueOptions options = {
            sizeof(options),
            DQTYPE_THREAD_CURRENT,
            DQTAT_COM_ASTA,
        };
        ABI::IDispatcherQueueController* controller;
        winrt::check_hresult(CreateDispatcherQueueController(options, &controller));
        *winrt::put_abi(m_dispatcherQueueController) = controller;

        // Create the compositor for our window.
        m_compositor = winrt::Compositor();
        ABI::IDesktopWindowTarget* target;
        winrt::check_hresult(m_compositor.as<ABI::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(m_hwnd, false, &target));
        *winrt::put_abi(m_target) = target;

        // Our composition tree:
        //
        // [root] ContainerVisual
        // \ [crosshairs border layer] LayerVisual
        //   \ [crosshairs border sprites]
        //     [crosshairs layer] LayerVisual
        //     \ [crosshairs sprites]

        m_root = m_compositor.CreateContainerVisual();
        m_root.RelativeSizeAdjustment({ 1.0f, 1.0f });
        m_target.Root(m_root);

        m_root.Opacity(m_crosshairs_opacity);

        m_crosshairs_border_layer = m_compositor.CreateLayerVisual();
        m_crosshairs_border_layer.RelativeSizeAdjustment({ 1.0f, 1.0f });
        m_root.Children().InsertAtTop(m_crosshairs_border_layer);
        m_crosshairs_border_layer.Opacity(1.0f);

        m_crosshairs_layer = m_compositor.CreateLayerVisual();
        m_crosshairs_layer.RelativeSizeAdjustment({ 1.0f, 1.0f });

        // Create the crosshairs sprites.
        m_left_crosshairs_border = m_compositor.CreateSpriteVisual();
        m_left_crosshairs_border.AnchorPoint({ 1.0f, 0.5f });
        m_left_crosshairs_border.Brush(m_compositor.CreateColorBrush(m_crosshairs_border_color));
        m_crosshairs_border_layer.Children().InsertAtTop(m_left_crosshairs_border);
        m_left_crosshairs = m_compositor.CreateSpriteVisual();
        m_left_crosshairs.AnchorPoint({ 1.0f, 0.5f });
        m_left_crosshairs.Brush(m_compositor.CreateColorBrush(m_crosshairs_color));
        m_crosshairs_layer.Children().InsertAtTop(m_left_crosshairs);

        m_right_crosshairs_border = m_compositor.CreateSpriteVisual();
        m_right_crosshairs_border.AnchorPoint({ 0.0f, 0.5f });
        m_right_crosshairs_border.Brush(m_compositor.CreateColorBrush(m_crosshairs_border_color));
        m_crosshairs_border_layer.Children().InsertAtTop(m_right_crosshairs_border);
        m_right_crosshairs = m_compositor.CreateSpriteVisual();
        m_right_crosshairs.AnchorPoint({ 0.0f, 0.5f });
        m_right_crosshairs.Brush(m_compositor.CreateColorBrush(m_crosshairs_color));
        m_crosshairs_layer.Children().InsertAtTop(m_right_crosshairs);

        m_top_crosshairs_border = m_compositor.CreateSpriteVisual();
        m_top_crosshairs_border.AnchorPoint({ 0.5f, 1.0f });
        m_top_crosshairs_border.Brush(m_compositor.CreateColorBrush(m_crosshairs_border_color));
        m_crosshairs_border_layer.Children().InsertAtTop(m_top_crosshairs_border);
        m_top_crosshairs = m_compositor.CreateSpriteVisual();
        m_top_crosshairs.AnchorPoint({ 0.5f, 1.0f });
        m_top_crosshairs.Brush(m_compositor.CreateColorBrush(m_crosshairs_color));
        m_crosshairs_layer.Children().InsertAtTop(m_top_crosshairs);

        m_bottom_crosshairs_border = m_compositor.CreateSpriteVisual();
        m_bottom_crosshairs_border.AnchorPoint({ 0.5f, 0.0f });
        m_bottom_crosshairs_border.Brush(m_compositor.CreateColorBrush(m_crosshairs_border_color));
        m_crosshairs_border_layer.Children().InsertAtTop(m_bottom_crosshairs_border);
        m_bottom_crosshairs = m_compositor.CreateSpriteVisual();
        m_bottom_crosshairs.AnchorPoint({ 0.5f, 0.0f });
        m_bottom_crosshairs.Brush(m_compositor.CreateColorBrush(m_crosshairs_color));
        m_crosshairs_layer.Children().InsertAtTop(m_bottom_crosshairs);

        m_crosshairs_border_layer.Children().InsertAtTop(m_crosshairs_layer);
        m_crosshairs_layer.Opacity(1.0f);

        UpdateCrosshairsPosition();

        return true;
    }
    catch (...)
    {
        return false;
    }
}

void InclusiveCrosshairs::UpdateCrosshairsPosition()
{
    POINT ptCursor;

    GetCursorPos(&ptCursor);

    HMONITOR cursorMonitor = MonitorFromPoint(ptCursor, MONITOR_DEFAULTTONEAREST);

    if (cursorMonitor == NULL)
    {
        return;
    }

    MONITORINFO monitorInfo;
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (!GetMonitorInfo(cursorMonitor, &monitorInfo))
    {
        return;
    }

    POINT ptMonitorUpperLeft;
    ptMonitorUpperLeft.x = monitorInfo.rcMonitor.left;
    ptMonitorUpperLeft.y = monitorInfo.rcMonitor.top;

    POINT ptMonitorBottomRight;
    ptMonitorBottomRight.x = monitorInfo.rcMonitor.right;
    ptMonitorBottomRight.y = monitorInfo.rcMonitor.bottom;

    // Convert everything to client coordinates.
    ScreenToClient(m_hwnd, &ptCursor);
    ScreenToClient(m_hwnd, &ptMonitorUpperLeft);
    ScreenToClient(m_hwnd, &ptMonitorBottomRight);

    // Position crosshairs components around the mouse pointer.
    m_left_crosshairs_border.Offset({ (float)ptCursor.x - m_crosshairs_radius + m_crosshairs_border_size, (float)ptCursor.y, .0f });
    m_left_crosshairs_border.Size({ (float)ptCursor.x - (float)ptMonitorUpperLeft.x - m_crosshairs_radius + m_crosshairs_border_size, m_crosshairs_thickness + m_crosshairs_border_size * 2 });
    m_left_crosshairs.Offset({ (float)ptCursor.x - m_crosshairs_radius, (float)ptCursor.y, .0f });
    m_left_crosshairs.Size({ (float)ptCursor.x - (float)ptMonitorUpperLeft.x - m_crosshairs_radius, m_crosshairs_thickness });

    m_right_crosshairs_border.Offset({ (float)ptCursor.x + m_crosshairs_radius - m_crosshairs_border_size, (float)ptCursor.y, .0f });
    m_right_crosshairs_border.Size({ (float)ptMonitorBottomRight.x - (float)ptCursor.x - m_crosshairs_radius + m_crosshairs_border_size, m_crosshairs_thickness + m_crosshairs_border_size * 2 });
    m_right_crosshairs.Offset({ (float)ptCursor.x + m_crosshairs_radius, (float)ptCursor.y, .0f });
    m_right_crosshairs.Size({ (float)ptMonitorBottomRight.x - (float)ptCursor.x - m_crosshairs_radius, m_crosshairs_thickness });

    m_top_crosshairs_border.Offset({ (float)ptCursor.x, (float)ptCursor.y - m_crosshairs_radius + m_crosshairs_border_size, .0f });
    m_top_crosshairs_border.Size({ m_crosshairs_thickness + m_crosshairs_border_size * 2, (float)ptCursor.y - (float)ptMonitorUpperLeft.y - m_crosshairs_radius + m_crosshairs_border_size });
    m_top_crosshairs.Offset({ (float)ptCursor.x, (float)ptCursor.y - m_crosshairs_radius, .0f });
    m_top_crosshairs.Size({ m_crosshairs_thickness, (float)ptCursor.y - (float)ptMonitorUpperLeft.y - m_crosshairs_radius });

    m_bottom_crosshairs_border.Offset({ (float)ptCursor.x, (float)ptCursor.y + m_crosshairs_radius - m_crosshairs_border_size, .0f });
    m_bottom_crosshairs_border.Size({ m_crosshairs_thickness + m_crosshairs_border_size * 2, (float)ptMonitorBottomRight.y - (float)ptCursor.y - m_crosshairs_radius + m_crosshairs_border_size });
    m_bottom_crosshairs.Offset({ (float)ptCursor.x, (float)ptCursor.y + m_crosshairs_radius, .0f });
    m_bottom_crosshairs.Size({ m_crosshairs_thickness, (float)ptMonitorBottomRight.y - (float)ptCursor.y - m_crosshairs_radius });

}

LRESULT CALLBACK InclusiveCrosshairs::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) noexcept
{
    if (nCode >= 0)
    {
        MSLLHOOKSTRUCT* hookData = (MSLLHOOKSTRUCT*)lParam;
        if (wParam == WM_MOUSEMOVE) {
            instance->UpdateCrosshairsPosition();
        }
    }
    return CallNextHookEx(0, nCode, wParam, lParam);
}

void InclusiveCrosshairs::StartDrawing()
{
    Logger::info("Start drawing crosshairs.");
    Trace::StartDrawingCrosshairs();
    m_visible = true;
    SetWindowPos(m_hwnd, HWND_TOPMOST, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN), 0);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, m_hinstance, 0);
    UpdateCrosshairsPosition();
}

void InclusiveCrosshairs::StopDrawing()
{
    Logger::info("Stop drawing crosshairs.");
    m_visible = false;
    ShowWindow(m_hwnd, SW_HIDE);
    UnhookWindowsHookEx(m_mouseHook);
    m_mouseHook = NULL;
}

void InclusiveCrosshairs::SwitchActivationMode()
{
    PostMessage(m_hwnd, WM_SWITCH_ACTIVATION_MODE, 0, 0);
}

void InclusiveCrosshairs::ApplySettings(InclusiveCrosshairsSettings& settings, bool applyToRunTimeObjects)
{
    m_crosshairs_radius = (float)settings.crosshairsRadius;
    m_crosshairs_thickness = (float)settings.crosshairsThickness;
    m_crosshairs_color = settings.crosshairsColor;
    m_crosshairs_opacity = max(0.f, min(1.f, (float)settings.crosshairsOpacity / 100.0f));
    m_crosshairs_border_color = settings.crosshairsBorderColor;
    m_crosshairs_border_size = (float)settings.crosshairsBorderSize;

    if (applyToRunTimeObjects)
    {
        // Runtime objects already created. Should update in the owner thread.
        auto dispatcherQueue = m_dispatcherQueueController.DispatcherQueue();
        InclusiveCrosshairsSettings localSettings = settings;
        bool enqueueSucceeded = dispatcherQueue.TryEnqueue([=]() {
            if (!m_destroyed)
            {
                // Apply new settings to runtime composition objects.
                m_left_crosshairs.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_color);
                m_right_crosshairs.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_color);
                m_top_crosshairs.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_color);
                m_bottom_crosshairs.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_color);
                m_left_crosshairs_border.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_border_color);
                m_right_crosshairs_border.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_border_color);
                m_top_crosshairs_border.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_border_color);
                m_bottom_crosshairs_border.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_border_color);
                m_root.Opacity(m_crosshairs_opacity);
                UpdateCrosshairsPosition();
            }
        });
        if (!enqueueSucceeded)
        {
            Logger::error("Couldn't enqueue message to update the crosshairs settings.");
        }
    }
}

void InclusiveCrosshairs::DestroyInclusiveCrosshairs()
{
    StopDrawing();
    PostQuitMessage(0);
}

LRESULT CALLBACK InclusiveCrosshairs::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_NCCREATE:
        instance->m_hwnd = hWnd;
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_CREATE:
        return instance->CreateInclusiveCrosshairs() ? 0 : -1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_SWITCH_ACTIVATION_MODE:
        if (instance->m_visible)
        {
            instance->StopDrawing();
        }
        else
        {
            instance->StartDrawing();
        }
        break;
    case WM_DESTROY:
        instance->DestroyInclusiveCrosshairs();
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool InclusiveCrosshairs::MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASS wc{};

    m_hinstance = hInstance;

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!GetClassInfoW(hInstance, m_className, &wc))
    {
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        wc.lpszClassName = m_className;

        if (!RegisterClassW(&wc))
        {
            return false;
        }
    }

    m_hwndOwner = CreateWindow(L"static", nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    DWORD exStyle = WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW;
    return CreateWindowExW(exStyle, m_className, m_windowTitle, WS_POPUP, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, m_hwndOwner, nullptr, hInstance, nullptr) != nullptr;
}

void InclusiveCrosshairs::Terminate()
{
    auto dispatcherQueue = m_dispatcherQueueController.DispatcherQueue();
    bool enqueueSucceeded = dispatcherQueue.TryEnqueue([=]() {
        m_destroyed = true;
        DestroyWindow(m_hwndOwner);
    });
    if (!enqueueSucceeded)
    {
        Logger::error("Couldn't enqueue message to destroy the window.");
    }
}

#pragma region InclusiveCrosshairs_API

void InclusiveCrosshairsApplySettings(InclusiveCrosshairsSettings& settings)
{
    if (InclusiveCrosshairs::instance != nullptr)
    {
        Logger::info("Applying settings.");
        InclusiveCrosshairs::instance->ApplySettings(settings, true);
    }
}

void InclusiveCrosshairsSwitch()
{
    if (InclusiveCrosshairs::instance != nullptr)
    {
        Logger::info("Switching activation mode.");
        InclusiveCrosshairs::instance->SwitchActivationMode();
    }
}

void InclusiveCrosshairsDisable()
{
    if (InclusiveCrosshairs::instance != nullptr)
    {
        Logger::info("Terminating the crosshairs instance.");
        InclusiveCrosshairs::instance->Terminate();
    }
}

bool InclusiveCrosshairsIsEnabled()
{
    return (InclusiveCrosshairs::instance != nullptr);
}

int InclusiveCrosshairsMain(HINSTANCE hInstance, InclusiveCrosshairsSettings& settings)
{
    Logger::info("Starting a crosshairs instance.");
    if (InclusiveCrosshairs::instance != nullptr)
    {
        Logger::error("A crosshairs instance was still working when trying to start a new one.");
        return 0;
    }

    // Perform application initialization:
    InclusiveCrosshairs crosshairs;
    InclusiveCrosshairs::instance = &crosshairs;
    crosshairs.ApplySettings(settings, false);
    if (!crosshairs.MyRegisterClass(hInstance))
    {
        Logger::error("Couldn't initialize a crosshairs instance.");
        InclusiveCrosshairs::instance = nullptr;
        return FALSE;
    }
    Logger::info("Initialized the crosshairs instance.");

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Logger::info("Crosshairs message loop ended.");
    InclusiveCrosshairs::instance = nullptr;

    return (int)msg.wParam;
}

#pragma endregion InclusiveCrosshairs_API
