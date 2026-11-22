#pragma once
// The panel's look: a dark palette, Noto Sans and the sizes of the panel's parts. The colours and
// metrics follow the user's F1DockTimers app, so both apps look like one family; the code is BatteryHub's own.
#include <windows.h>

#include <string>

namespace theme {

constexpr COLORREF kAccent = RGB(0xBC, 0xC2, 0xFF);     // selected tab, highlights
constexpr COLORREF kOnAccent = RGB(0x26, 0x2C, 0x67);   // text on the accent
constexpr COLORREF kSurface = RGB(0x1B, 0x1B, 0x29);    // panel background
constexpr COLORREF kOnSurface = RGB(0xE4, 0xE1, 0xEC);  // text
constexpr COLORREF kMuted = RGB(0xC6, 0xC5, 0xD0);      // secondary text, icons
constexpr COLORREF kFaint = RGB(0x9E, 0x9E, 0xA6);      // footer (muted, 25% darker)
constexpr COLORREF kOutline = RGB(0x46, 0x46, 0x4F);    // divider, borders
constexpr COLORREF kError = RGB(0xFF, 0xB4, 0xAB);      // low battery, destructive actions
constexpr COLORREF kGo = RGB(0x1F, 0xBF, 0x5B);         // charging
constexpr COLORREF kField = RGB(0x24, 0x24, 0x3A);      // input fields, cards
constexpr COLORREF kButton = RGB(0x2A, 0x2A, 0x40);     // buttons
constexpr COLORREF kButtonHover = RGB(0x33, 0x33, 0x4C);
constexpr COLORREF kPressed = RGB(0x38, 0x38, 0x52);

// Registers the built-in Noto Sans (SIL OFL 1.1, res/fonts) for this process. Call once at startup.
void LoadFonts(HINSTANCE inst);
// "Noto Sans" when it is loaded and has every character of `text`, else "Segoe UI" (Arabic, CJK, Hebrew,
// Thai, Vietnamese, ...).
const wchar_t* Family(const std::wstring& text);
// a * (1 - t) + b * t
COLORREF Mix(COLORREF a, COLORREF b, float t);

}  // namespace theme
