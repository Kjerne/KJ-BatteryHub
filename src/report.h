#pragma once
// "Request support for a device": a small window where a user picks a device that is not shown, sees the
// request word for word, and clicks Open on GitHub to get a filled-in new issue on this fork's GitHub page
// in their browser. The app itself sends nothing; the user submits the issue there.
#include <windows.h>

#include <string>

namespace report {

// Opens the window (or brings it to the front if it is already open). With no owner (`--report`), closing
// the window ends the message loop.
void Show(HINSTANCE inst, HWND owner);

// Lets Tab / Enter / Esc work inside the window; call from the message loop before TranslateMessage.
bool PreTranslate(MSG* msg);

// Extra sections for `--dump` and the full report: HID devices that could not be opened, every USB device
// (including ones without a HID interface, e.g. audio-only or WinUSB dongles and serial ports) and the
// Bluetooth devices Windows knows about.
std::wstring DumpExtras();

}  // namespace report
