#include "pch.h"
#include "MonitorWorkAreaHandler.h"
#include "VirtualDesktop.h"
#include "util.h"

winrt::com_ptr<IWorkArea> MonitorWorkAreaHandler::GetWorkArea(const GUID& desktopId, HMONITOR monitor)
{
    auto desktopIt = workAreaMap.find(desktopId);
    if (desktopIt != std::end(workAreaMap))
    {
        auto& perDesktopData = desktopIt->second;
        auto monitorIt = perDesktopData.find(monitor);
        if (monitorIt != std::end(perDesktopData))
        {
            return monitorIt->second;
        }
    }
    return nullptr;
}

winrt::com_ptr<IWorkArea> MonitorWorkAreaHandler::GetWorkAreaFromCursor(const GUID& desktopId)
{
    auto allMonitorsWorkArea = GetWorkArea(desktopId, NULL);
    if (allMonitorsWorkArea)
    {
        // First, check if there's a work area spanning all monitors (signalled by the NULL monitor handle)
        return allMonitorsWorkArea;
    }
    else
    {
        // Otherwise, look for the work area based on cursor position
        POINT cursorPoint;
        if (!GetCursorPos(&cursorPoint))
        {
            return nullptr;
        }

        return GetWorkArea(desktopId, MonitorFromPoint(cursorPoint, MONITOR_DEFAULTTONULL));
    }
}

winrt::com_ptr<IWorkArea> MonitorWorkAreaHandler::GetWorkArea(HWND window, const GUID& desktopId)
{
    auto allMonitorsWorkArea = GetWorkArea(desktopId, NULL);
    if (allMonitorsWorkArea)
    {
        // First, check if there's a work area spanning all monitors (signalled by the NULL monitor handle)
        return allMonitorsWorkArea;
    }
    else
    {
        // Otherwise, look for the work area based on the window's position
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
        return GetWorkArea(desktopId, monitor);
    }
}

const std::unordered_map<HMONITOR, winrt::com_ptr<IWorkArea>>& MonitorWorkAreaHandler::GetWorkAreasByDesktopId(const GUID& desktopId)
{
    if (workAreaMap.contains(desktopId))
    {
        return workAreaMap[desktopId];
    }
    static const std::unordered_map<HMONITOR, winrt::com_ptr<IWorkArea>> empty;
    return empty;
}

std::vector<winrt::com_ptr<IWorkArea>> MonitorWorkAreaHandler::GetAllWorkAreas()
{
    std::vector<winrt::com_ptr<IWorkArea>> workAreas{};
    for (const auto& [desktopId, perDesktopData] : workAreaMap)
    {
        std::transform(std::begin(perDesktopData),
                       std::end(perDesktopData),
                       std::back_inserter(workAreas),
                       [](const auto& item) { return item.second; });
    }
    return workAreas;
}

void MonitorWorkAreaHandler::AddWorkArea(const GUID& desktopId, HMONITOR monitor, winrt::com_ptr<IWorkArea>& workArea)
{
    if (!workAreaMap.contains(desktopId))
    {
        workAreaMap[desktopId] = {};
    }
    auto& perDesktopData = workAreaMap[desktopId];
    perDesktopData[monitor] = std::move(workArea);
}

bool MonitorWorkAreaHandler::IsNewWorkArea(const GUID& desktopId, HMONITOR monitor)
{
    if (workAreaMap.contains(desktopId))
    {
        const auto& perDesktopData = workAreaMap[desktopId];
        if (perDesktopData.contains(monitor))
        {
            return false;
        }
    }
    return true;
}

void MonitorWorkAreaHandler::RegisterUpdates(const std::vector<GUID>& active)
{
    std::unordered_set<GUID> activeVirtualDesktops(std::begin(active), std::end(active));
    for (auto desktopIt = std::begin(workAreaMap); desktopIt != std::end(workAreaMap);)
    {
        auto activeIt = activeVirtualDesktops.find(desktopIt->first);
        if (activeIt == std::end(activeVirtualDesktops))
        {
            // virtual desktop deleted, remove entry from the map
            desktopIt = workAreaMap.erase(desktopIt);
        }
        else
        {
            activeVirtualDesktops.erase(desktopIt->first); // virtual desktop already in map, skip it
            ++desktopIt;
        }
    }
    // register new virtual desktops, if any
    for (const auto& id : activeVirtualDesktops)
    {
        workAreaMap[id] = {};
    }
}

void MonitorWorkAreaHandler::Clear()
{
    workAreaMap.clear();
}

void MonitorWorkAreaHandler::UpdateZoneColors(const ZoneColors& colors)
{
    for (const auto& workArea : workAreaMap)
    {
        for (const auto& workAreaPtr : workArea.second)
        {
            workAreaPtr.second->SetZoneColors(colors);
        }
    }
}

void MonitorWorkAreaHandler::UpdateOverlappingAlgorithm(OverlappingZonesAlgorithm overlappingAlgorithm)
{
    for (const auto& workArea : workAreaMap)
    {
        for (const auto& workAreaPtr : workArea.second)
        {
            workAreaPtr.second->SetOverlappingZonesAlgorithm(overlappingAlgorithm);
        }
    }
}
