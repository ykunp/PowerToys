#include "pch.h"
#include "AlwaysOnTop.h"

#include <common/display/dpi_aware.h>
#include <common/utils/game_mode.h>
#include <common/utils/resources.h>
#include <common/utils/winapi_error.h>
#include <common/utils/process_path.h>

#include <WinHookEventIDs.h>

namespace NonLocalizable
{
    const static wchar_t* TOOL_WINDOW_CLASS_NAME = L"AlwaysOnTopWindow";
    const static wchar_t* WINDOW_IS_PINNED_PROP = L"AlwaysOnTop_Pinned";
}

// TODO: move to common utils
bool find_app_name_in_path(const std::wstring& where, const std::vector<std::wstring>& what)
{
    for (const auto& row : what)
    {
        const auto pos = where.rfind(row);
        const auto last_slash = where.rfind('\\');
        //Check that row occurs in where, and its last occurrence contains in itself the first character after the last backslash.
        if (pos != std::wstring::npos && pos <= last_slash + 1 && pos + row.length() > last_slash)
        {
            return true;
        }
    }
    return false;
}

bool isExcluded(HWND window)
{
    auto processPath = get_process_path(window);
    CharUpperBuffW(processPath.data(), (DWORD)processPath.length());
    return find_app_name_in_path(processPath, AlwaysOnTopSettings::settings().excludedApps);
}

AlwaysOnTop::AlwaysOnTop() :
    SettingsObserver({SettingId::FrameEnabled, SettingId::Hotkey, SettingId::ExcludeApps}),
    m_hinstance(reinterpret_cast<HINSTANCE>(&__ImageBase))
{
    s_instance = this;
    DPIAware::EnableDPIAwarenessForThisProcess();

    if (InitMainWindow())
    {
        InitializeWinhookEventIds();

        AlwaysOnTopSettings::instance().InitFileWatcher();
        AlwaysOnTopSettings::instance().LoadSettings();

        RegisterHotkey();
        SubscribeToEvents();
        StartTrackingTopmostWindows();
    }
    else
    {
        Logger::error("Failed to init AlwaysOnTop module");
        // TODO: show localized message
    }
}

AlwaysOnTop::~AlwaysOnTop()
{
    CleanUp();
}

bool AlwaysOnTop::InitMainWindow()
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = WndProc_Helper;
    wcex.hInstance = m_hinstance;
    wcex.lpszClassName = NonLocalizable::TOOL_WINDOW_CLASS_NAME;
    RegisterClassExW(&wcex);

    m_window = CreateWindowExW(WS_EX_TOOLWINDOW, NonLocalizable::TOOL_WINDOW_CLASS_NAME, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, m_hinstance, this);
    if (!m_window)
    {
        Logger::error(L"Failed to create AlwaysOnTop window: {}", get_last_error_or_default(GetLastError()));
        return false;
    }

    return true;
}

void AlwaysOnTop::SettingsUpdate(SettingId id)
{
    switch (id)
    {
    case SettingId::Hotkey:
    {
        RegisterHotkey();
    }
    break;
    case SettingId::FrameEnabled:
    {
        if (AlwaysOnTopSettings::settings().enableFrame)
        {
            for (auto& iter : m_topmostWindows)
            {
                if (!iter.second)
                {
                    AssignBorder(iter.first);
                }
            }
        }
        else
        {
            for (auto& iter : m_topmostWindows)
            {
                iter.second = nullptr;
            }
        }    
    }
    break;
    case SettingId::ExcludeApps:
    {
        std::vector<HWND> toErase{};
        for (const auto& [window, border] : m_topmostWindows)
        {
            if (isExcluded(window))
            {
                UnpinTopmostWindow(window);
                toErase.push_back(window);
            }
        }

        for (const auto window: toErase)
        {
            m_topmostWindows.erase(window);
        }
    }
    break;
    default:
        break;
    }
}

LRESULT AlwaysOnTop::WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    if (message == WM_HOTKEY)
    {
        if (HWND fw{ GetForegroundWindow() })
        {
            ProcessCommand(fw);
        }
    }
    else if (message == WM_PRIV_SETTINGS_CHANGED)
    {
        AlwaysOnTopSettings::instance().LoadSettings();
    }
    
    return 0;
}

void AlwaysOnTop::ProcessCommand(HWND window)
{
    bool gameMode = detect_game_mode();
    if (AlwaysOnTopSettings::settings().blockInGameMode && gameMode)
    {
        return;
    }

    if (isExcluded(window))
    {
        return;
    }

    Sound::Type soundType = Sound::Type::Off;
    bool topmost = IsTopmost(window);
    if (topmost)
    {
        if (UnpinTopmostWindow(window))
        {
            auto iter = m_topmostWindows.find(window);
            if (iter != m_topmostWindows.end())
            {
                m_topmostWindows.erase(iter);
            }
        }
    }
    else
    {
        if (PinTopmostWindow(window))
        {
            soundType = Sound::Type::On;
            AssignBorder(window);
        }
    }

    if (AlwaysOnTopSettings::settings().enableSound)
    {
        m_sound.Play(soundType);    
    }
}

void AlwaysOnTop::StartTrackingTopmostWindows()
{
    using result_t = std::vector<HWND>;
    result_t result;

    auto enumWindows = [](HWND hwnd, LPARAM param) -> BOOL {
        if (!IsWindowVisible(hwnd))
        {
            return TRUE;
        }

        if (isExcluded(hwnd))
        {
            return TRUE;
        }

        auto windowName = GetWindowTextLength(hwnd);
        if (windowName > 0)
        {
            result_t& result = *reinterpret_cast<result_t*>(param);
            result.push_back(hwnd);
        }

        return TRUE;
    };

    EnumWindows(enumWindows, reinterpret_cast<LPARAM>(&result));

    for (HWND window : result)
    {
        if (IsPinned(window))
        {
            AssignBorder(window);
        }
    }
}

bool AlwaysOnTop::AssignBorder(HWND window)
{
    if (m_virtualDesktopUtils.IsWindowOnCurrentDesktop(window) && AlwaysOnTopSettings::settings().enableFrame)
    {
        auto border = WindowBorder::Create(window, m_hinstance);
        if (border)
        {
            m_topmostWindows[window] = std::move(border);
        }
    }
    else
    {
        m_topmostWindows[window] = nullptr;
    }
    
    return true;
}

void AlwaysOnTop::RegisterHotkey() const
{
    UnregisterHotKey(m_window, static_cast<int>(HotkeyId::Pin));
    RegisterHotKey(m_window, static_cast<int>(HotkeyId::Pin), AlwaysOnTopSettings::settings().hotkey.get_modifiers(), AlwaysOnTopSettings::settings().hotkey.get_code());
}

void AlwaysOnTop::SubscribeToEvents()
{
    // subscribe to windows events
    std::array<DWORD, 6> events_to_subscribe = {
        EVENT_OBJECT_LOCATIONCHANGE,
        EVENT_SYSTEM_MINIMIZESTART,
        EVENT_SYSTEM_MINIMIZEEND,
        EVENT_SYSTEM_MOVESIZEEND,
        EVENT_OBJECT_DESTROY,
        EVENT_OBJECT_NAMECHANGE
    };

    for (const auto event : events_to_subscribe)
    {
        auto hook = SetWinEventHook(event, event, nullptr, WinHookProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook)
        {
            m_staticWinEventHooks.emplace_back(hook);
        }
        else
        {
            Logger::error(L"Failed to set win event hook");
        }
    }
}

void AlwaysOnTop::UnpinAll()
{
    for (const auto& [topWindow, border] : m_topmostWindows)
    {
        if (!UnpinTopmostWindow(topWindow))
        {
            Logger::error(L"Unpinning topmost window failed");
        }
    }

    m_topmostWindows.clear();
}

void AlwaysOnTop::CleanUp()
{
    UnpinAll();
    if (m_window)
    {
        DestroyWindow(m_window);
        m_window = nullptr;
    }

    UnregisterClass(NonLocalizable::TOOL_WINDOW_CLASS_NAME, reinterpret_cast<HINSTANCE>(&__ImageBase));
}

bool AlwaysOnTop::IsTopmost(HWND window) const noexcept
{
    int exStyle = GetWindowLong(window, GWL_EXSTYLE);
    return (exStyle & WS_EX_TOPMOST) == WS_EX_TOPMOST;
}

bool AlwaysOnTop::IsPinned(HWND window) const noexcept
{
    auto handle = GetProp(window, NonLocalizable::WINDOW_IS_PINNED_PROP);
    return (handle != NULL);
}

bool AlwaysOnTop::PinTopmostWindow(HWND window) const noexcept
{
    if (!SetProp(window, NonLocalizable::WINDOW_IS_PINNED_PROP, (HANDLE)1))
    {
        Logger::error(L"SetProp failed");
    }
    return SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

bool AlwaysOnTop::UnpinTopmostWindow(HWND window) const noexcept
{
    RemoveProp(window, NonLocalizable::WINDOW_IS_PINNED_PROP);
    return SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

bool AlwaysOnTop::IsTracked(HWND window) const noexcept
{
    auto iter = m_topmostWindows.find(window);
    return (iter != m_topmostWindows.end());
}

void AlwaysOnTop::HandleWinHookEvent(WinHookEvent* data) noexcept
{
    if (!AlwaysOnTopSettings::settings().enableFrame)
    {
        return;
    }

    switch (data->event)
    {
    case EVENT_OBJECT_LOCATIONCHANGE:
    {
        auto iter = m_topmostWindows.find(data->hwnd);
        if (iter != m_topmostWindows.end())
        {
            const auto& border = iter->second;
            if (border)
            {
                border->UpdateBorderPosition();
            }
        }
    }
    break;
    case EVENT_SYSTEM_MINIMIZESTART:
    {
        auto iter = m_topmostWindows.find(data->hwnd);
        if (iter != m_topmostWindows.end())
        {
            m_topmostWindows[data->hwnd] = nullptr;
        }
    }
    break;
    case EVENT_SYSTEM_MINIMIZEEND:
    {
        auto iter = m_topmostWindows.find(data->hwnd);
        if (iter != m_topmostWindows.end())
        {
            AssignBorder(data->hwnd);
        }
    }
    break;
    case EVENT_SYSTEM_MOVESIZEEND:
    {
        auto iter = m_topmostWindows.find(data->hwnd);
        if (iter != m_topmostWindows.end())
        {
            const auto& border = iter->second;
            if (border)
            {
                border->UpdateBorderPosition();
            }
        }
    }
    break;
    case EVENT_OBJECT_DESTROY:
    {
        auto iter = m_topmostWindows.find(data->hwnd);
        if (iter != m_topmostWindows.end())
        {
            m_topmostWindows.erase(iter);
        }
    }
    break;
    case EVENT_OBJECT_NAMECHANGE:
    {
        // The accessibility name of the desktop window changes whenever the user
        // switches virtual desktops.
        if (data->hwnd == GetDesktopWindow())
        {
            VirtualDesktopSwitchedHandle();
        }
    }
    break;
    default:
        break;
    }
}

void AlwaysOnTop::VirtualDesktopSwitchedHandle()
{
    for (const auto& [window, border] : m_topmostWindows)
    {
        if (m_virtualDesktopUtils.IsWindowOnCurrentDesktop(window))
        {
            AssignBorder(window);
        }
        else
        {
            m_topmostWindows[window] = nullptr;
        }
    }
}