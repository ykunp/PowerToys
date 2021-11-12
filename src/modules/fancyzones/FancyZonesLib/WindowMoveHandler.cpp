#include "pch.h"
#include "WindowMoveHandler.h"

#include <common/display/dpi_aware.h>
#include <common/notifications/notifications.h>
#include <common/notifications/dont_show_again.h>
#include <common/utils/elevation.h>
#include <common/utils/resources.h>

#include "FancyZonesData.h"
#include "Settings.h"
#include "WorkArea.h"
#include "util.h"

// Non-Localizable strings
namespace NonLocalizable
{
    const wchar_t FancyZonesRunAsAdminInfoPage[] = L"https://aka.ms/powertoysDetectedElevatedHelp";
    const wchar_t ToastNotificationButtonUrl[] = L"powertoys://cant_drag_elevated_disable/";
}

namespace WindowMoveHandlerUtils
{
    bool IsCursorTypeIndicatingSizeEvent()
    {
        CURSORINFO cursorInfo = { 0 };
        cursorInfo.cbSize = sizeof(cursorInfo);

        if (::GetCursorInfo(&cursorInfo))
        {
            if (::LoadCursor(NULL, IDC_SIZENS) == cursorInfo.hCursor)
            {
                return true;
            }
            if (::LoadCursor(NULL, IDC_SIZEWE) == cursorInfo.hCursor)
            {
                return true;
            }
            if (::LoadCursor(NULL, IDC_SIZENESW) == cursorInfo.hCursor)
            {
                return true;
            }
            if (::LoadCursor(NULL, IDC_SIZENWSE) == cursorInfo.hCursor)
            {
                return true;
            }
        }
        return false;
    }
}

WindowMoveHandler::WindowMoveHandler(const winrt::com_ptr<IFancyZonesSettings>& settings, const std::function<void()>& keyUpdateCallback) :
    m_settings(settings),
    m_mouseState(false),
    m_mouseHook(std::bind(&WindowMoveHandler::OnMouseDown, this)),
    m_shiftKeyState(keyUpdateCallback),
    m_ctrlKeyState(keyUpdateCallback),
    m_keyUpdateCallback(keyUpdateCallback)
{
}

void WindowMoveHandler::MoveSizeStart(HWND window, HMONITOR monitor, POINT const& ptScreen, const std::unordered_map<HMONITOR, winrt::com_ptr<IWorkArea>>& workAreaMap) noexcept
{
    if (!FancyZonesUtils::IsCandidateForZoning(window, m_settings->GetSettings()->excludedAppsArray) || WindowMoveHandlerUtils::IsCursorTypeIndicatingSizeEvent())
    {
        return;
    }

    m_draggedWindowInfo.hasNoVisibleOwner = FancyZonesUtils::HasNoVisibleOwner(window);
    m_draggedWindowInfo.isStandardWindow = FancyZonesUtils::IsStandardWindow(window);
    m_inDragging = true;

    auto iter = workAreaMap.find(monitor);
    if (iter == end(workAreaMap))
    {
        return;
    }

    m_draggedWindow = window;

    if (m_settings->GetSettings()->mouseSwitch)
    {
        m_mouseHook.enable();
    }

    m_shiftKeyState.enable();
    m_ctrlKeyState.enable();

    // This updates m_dragEnabled depending on if the shift key is being held down
    UpdateDragState();

    // Notifies user if unable to drag elevated window
    WarnIfElevationIsRequired(window);

    if (m_dragEnabled)
    {
        m_draggedWindowWorkArea = iter->second;
        SetWindowTransparency(m_draggedWindow);
        m_draggedWindowWorkArea->MoveSizeEnter(m_draggedWindow);
        if (m_settings->GetSettings()->showZonesOnAllMonitors)
        {
            for (auto [keyMonitor, workArea] : workAreaMap)
            {
                // Skip calling ShowZoneWindow for iter->second (m_draggedWindowWorkArea) since it
                // was already called in MoveSizeEnter
                const bool moveSizeEnterCalled = workArea == m_draggedWindowWorkArea;
                if (workArea && !moveSizeEnterCalled)
                {
                    workArea->ShowZoneWindow();
                }
            }
        }
    }
    else if (m_draggedWindowWorkArea)
    {
        ResetWindowTransparency();
        m_draggedWindowWorkArea = nullptr;
        for (auto [keyMonitor, workArea] : workAreaMap)
        {
            if (workArea)
            {
                workArea->HideZoneWindow();
            }
        }
    }

    auto workArea = workAreaMap.find(monitor);
    if (workArea != workAreaMap.end())
    {
        const auto workAreaPtr = workArea->second;
        const auto zoneSet = workAreaPtr->ZoneSet();
        if (zoneSet)
        {
            zoneSet->DismissWindow(window);
        }
    }
}

void WindowMoveHandler::MoveSizeUpdate(HMONITOR monitor, POINT const& ptScreen, const std::unordered_map<HMONITOR, winrt::com_ptr<IWorkArea>>& workAreaMap) noexcept
{
    if (!m_inDragging)
    {
        return;
    }

    // This updates m_dragEnabled depending on if the shift key is being held down.
    UpdateDragState();

    if (m_draggedWindowWorkArea)
    {
        // Update the WorkArea already handling move/size
        if (!m_dragEnabled)
        {
            // Drag got disabled, tell it to cancel and hide all windows
            m_draggedWindowWorkArea = nullptr;
            ResetWindowTransparency();

            for (auto [keyMonitor, workArea] : workAreaMap)
            {
                if (workArea)
                {
                    workArea->HideZoneWindow();
                }
            }
        }
        else
        {
            auto iter = workAreaMap.find(monitor);
            if (iter != workAreaMap.end())
            {
                if (iter->second != m_draggedWindowWorkArea)
                {
                    // The drag has moved to a different monitor.
                    m_draggedWindowWorkArea->ClearSelectedZones();
                    if (!m_settings->GetSettings()->showZonesOnAllMonitors)
                    {
                        m_draggedWindowWorkArea->HideZoneWindow();
                    }

                    m_draggedWindowWorkArea = iter->second;
                    m_draggedWindowWorkArea->MoveSizeEnter(m_draggedWindow);
                }

                for (auto [keyMonitor, workArea] : workAreaMap)
                {
                    workArea->MoveSizeUpdate(ptScreen, m_dragEnabled, m_ctrlKeyState.state());
                }
            }
        }
    }
    else if (m_dragEnabled)
    {
        // We'll get here if the user presses/releases shift while dragging.
        // Restart the drag on the WorkArea that m_draggedWindow is on
        MoveSizeStart(m_draggedWindow, monitor, ptScreen, workAreaMap);

        // m_dragEnabled could get set to false if we're moving an elevated window.
        // In that case do not proceed.
        if (m_dragEnabled)
        {
            MoveSizeUpdate(monitor, ptScreen, workAreaMap);
        }
    }
}

void WindowMoveHandler::MoveSizeEnd(HWND window, POINT const& ptScreen, const std::unordered_map<HMONITOR, winrt::com_ptr<IWorkArea>>& workAreaMap) noexcept
{
    if (window != m_draggedWindow)
    {
        return;
    }

    m_mouseHook.disable();
    m_shiftKeyState.disable();
    m_ctrlKeyState.disable();

    if (m_draggedWindowWorkArea)
    {
        auto workArea = std::move(m_draggedWindowWorkArea);
        ResetWindowTransparency();

        bool hasNoVisibleOwner = FancyZonesUtils::HasNoVisibleOwner(window);
        bool isStandardWindow = FancyZonesUtils::IsStandardWindow(window);

        if ((isStandardWindow == false && hasNoVisibleOwner == true &&
             m_draggedWindowInfo.isStandardWindow == true && m_draggedWindowInfo.hasNoVisibleOwner == true) ||
            FancyZonesUtils::IsWindowMaximized(window))
        {
            // Abort the zoning, this is a Chromium based tab that is merged back with an existing window
            // or if the window is maximized by Windows when the cursor hits the screen top border
        }
        else
        {
            workArea->MoveSizeEnd(m_draggedWindow, ptScreen);
        }
    }
    else
    {
        if (m_settings->GetSettings()->restoreSize)
        {
            if (WindowMoveHandlerUtils::IsCursorTypeIndicatingSizeEvent())
            {
                ::RemoveProp(window, ZonedWindowProperties::PropertyRestoreSizeID);
            }
            else if (!FancyZonesUtils::IsWindowMaximized(window))
            {
                FancyZonesUtils::RestoreWindowSize(window);
            }
        }

        auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
        if (monitor)
        {
            auto workArea = workAreaMap.find(monitor);
            if (workArea != workAreaMap.end())
            {
                const auto workAreaPtr = workArea->second;
                const auto zoneSet = workAreaPtr->ZoneSet();
                if (zoneSet)
                {
                    wil::unique_cotaskmem_string guidString;
                    if (SUCCEEDED_LOG(StringFromCLSID(zoneSet->Id(), &guidString)))
                    {
                        FancyZonesDataInstance().RemoveAppLastZone(window, workAreaPtr->UniqueId(), guidString.get());
                    }
                }
            }
        }
        ::RemoveProp(window, ZonedWindowProperties::PropertyMultipleZoneID);
    }

    m_inDragging = false;
    m_dragEnabled = false;
    m_mouseState = false;
    m_draggedWindow = nullptr;

    // Also, hide all windows (regardless of settings)
    for (auto [keyMonitor, workArea] : workAreaMap)
    {
        if (workArea)
        {
            workArea->HideZoneWindow();
        }
    }
}

void WindowMoveHandler::MoveWindowIntoZoneByIndexSet(HWND window, const ZoneIndexSet& indexSet, winrt::com_ptr<IWorkArea> workArea, bool suppressMove) noexcept
{
    if (window != m_draggedWindow)
    {
        workArea->MoveWindowIntoZoneByIndexSet(window, indexSet, suppressMove);
    }
}

bool WindowMoveHandler::MoveWindowIntoZoneByDirectionAndIndex(HWND window, DWORD vkCode, bool cycle, winrt::com_ptr<IWorkArea> workArea) noexcept
{
    return workArea && workArea->MoveWindowIntoZoneByDirectionAndIndex(window, vkCode, cycle);
}

bool WindowMoveHandler::MoveWindowIntoZoneByDirectionAndPosition(HWND window, DWORD vkCode, bool cycle, winrt::com_ptr<IWorkArea> workArea) noexcept
{
    return workArea && workArea->MoveWindowIntoZoneByDirectionAndPosition(window, vkCode, cycle);
}

bool WindowMoveHandler::ExtendWindowByDirectionAndPosition(HWND window, DWORD vkCode, winrt::com_ptr<IWorkArea> workArea) noexcept
{
    return workArea && workArea->ExtendWindowByDirectionAndPosition(window, vkCode);
}

void WindowMoveHandler::WarnIfElevationIsRequired(HWND window) noexcept
{
    using namespace notifications;
    using namespace NonLocalizable;
    using namespace FancyZonesUtils;

    static bool warning_shown = false;
    if (!is_process_elevated() && IsProcessOfWindowElevated(window))
    {
        m_dragEnabled = false;
        if (!warning_shown && !is_toast_disabled(CantDragElevatedDontShowAgainRegistryPath, CantDragElevatedDisableIntervalInDays))
        {
            std::vector<action_t> actions = {
                link_button{ GET_RESOURCE_STRING(IDS_CANT_DRAG_ELEVATED_LEARN_MORE), FancyZonesRunAsAdminInfoPage },
                link_button{ GET_RESOURCE_STRING(IDS_CANT_DRAG_ELEVATED_DIALOG_DONT_SHOW_AGAIN), ToastNotificationButtonUrl }
            };
            show_toast_with_activations(GET_RESOURCE_STRING(IDS_CANT_DRAG_ELEVATED),
                                        GET_RESOURCE_STRING(IDS_FANCYZONES),
                                        {},
                                        std::move(actions));
            warning_shown = true;
        }
    }
}

void WindowMoveHandler::UpdateDragState() noexcept
{
    if (m_settings->GetSettings()->shiftDrag)
    {
        m_dragEnabled = (m_shiftKeyState.state() ^ m_mouseState);
    }
    else
    {
        m_dragEnabled = !(m_shiftKeyState.state() ^ m_mouseState);
    }
}

void WindowMoveHandler::SetWindowTransparency(HWND window) noexcept
{
    if (m_settings->GetSettings()->makeDraggedWindowTransparent)
    {
        m_windowTransparencyProperties.draggedWindowExstyle = GetWindowLong(window, GWL_EXSTYLE);

        m_windowTransparencyProperties.draggedWindow = window;
        SetWindowLong(window,
                      GWL_EXSTYLE,
                      m_windowTransparencyProperties.draggedWindowExstyle | WS_EX_LAYERED);

        GetLayeredWindowAttributes(window, &m_windowTransparencyProperties.draggedWindowCrKey, &m_windowTransparencyProperties.draggedWindowInitialAlpha, &m_windowTransparencyProperties.draggedWindowDwFlags);

        SetLayeredWindowAttributes(window, 0, (255 * 50) / 100, LWA_ALPHA);
    }
}

void WindowMoveHandler::ResetWindowTransparency() noexcept
{
    if (m_settings->GetSettings()->makeDraggedWindowTransparent && m_windowTransparencyProperties.draggedWindow != nullptr)
    {
        SetLayeredWindowAttributes(m_windowTransparencyProperties.draggedWindow, m_windowTransparencyProperties.draggedWindowCrKey, m_windowTransparencyProperties.draggedWindowInitialAlpha, m_windowTransparencyProperties.draggedWindowDwFlags);
        SetWindowLong(m_windowTransparencyProperties.draggedWindow, GWL_EXSTYLE, m_windowTransparencyProperties.draggedWindowExstyle);
        m_windowTransparencyProperties.draggedWindow = nullptr;
    }
}
