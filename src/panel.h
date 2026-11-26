#pragma once
// The panel: a popup above the taskbar (a click on the taskbar strip or a tray icon) with three tabs,
// Devices, History and Settings. Drawn by hand into one layered window (no child controls), so it stays
// small and looks the same everywhere. The app (main.cpp) supplies the data and runs the commands; every
// setting is one of the tray menu's commands, so it saves the same registry values as the menu.
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace panel {

enum class Tab { Devices, History, Settings };

struct Device {
    std::wstring key, name;
    std::wstring level;        // "78%", "64% (last known)", "unknown"
    std::wstring status;       // "charging, full in ~40 min", "asleep", "off / not connected", ...
    std::wstring connection;   // "Bluetooth", "2.4 GHz receiver (Logitech)"
    std::wstring timeToEmpty;  // "~3 d 4 h", empty when there is no estimate
    std::wstring details;      // battery tracking: "2 h since charge · Last reading: 1 min ago"
    int percent = -1;          // the shown level, -1 = none
    bool charging = false, connected = true, dim = false, hidden = false, low = false;
    wchar_t glyph = 0;         // Segoe Fluent Icons symbol for the device type
    COLORREF color = CLR_INVALID;  // brand or own colour of the device (CLR_INVALID: none)
    int glow = 0;                  // glow behind its icon in that colour, percent (100 = default, 0 = none)
};

struct Choice {
    std::wstring label;
    UINT command = 0;
    bool selected = false;
    bool enabled = true;
};

struct Setting {
    enum class Kind { Page, Section, Toggle, Select, Button, Hint } kind = Kind::Toggle;
    std::wstring label;
    UINT command = 0;      // Toggle, Button
    bool checked = false;  // Toggle
    bool enabled = true;
    std::vector<Choice> choices;  // Select
};

struct Host {
    std::function<std::vector<Device>()> devices;                // in the app's order
    std::function<std::vector<std::wstring>()> unsupported;      // detected, without battery info
    std::function<std::vector<Setting>()> settings;
    std::function<void(UINT command)> command;                   // a tray menu command
    std::function<std::vector<Choice>(const std::wstring& key)> deviceActions;  // show / hide, move, rename, alert
    std::function<std::wstring()> footer;                        // "Updated 14:02"
    std::function<void()> refresh;                               // read the batteries now
    std::function<void(const std::wstring& key, bool custom)> historyWindow;  // the larger history window
};

void Init(HINSTANCE inst, const Host& host);
// Opens the panel on a tab; History opens on `key` when given.
void Show(Tab tab, const std::wstring& key = L"");
// Left-click on the strip or a tray icon: opens the panel, or closes it when it is open.
void Toggle(Tab tab = Tab::Devices, const std::wstring& key = L"");
void Hide();
bool Visible();
// The devices or the settings changed: redraws the open panel.
void Update();
// `--panel-shots <folder>`: draws every tab and settings page at 150% into .bmp files (docs, comparisons).
bool SaveShots(const std::wstring& folder);

}  // namespace panel
