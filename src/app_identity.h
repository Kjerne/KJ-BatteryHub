#pragma once
// Where KJ-BatteryHub keeps its data on Windows, in one place:
//   %LOCALAPPDATA%\KJ-BatteryHub   battery_history.tsv, status.json, updates folder
//   %APPDATA%\KJ-BatteryHub\lang   the user's own translations
//   HKCU\Software\KJ-BatteryHub    settings
//   HKCU\...\Run "KJ-BatteryHub"   "Start with Windows"
// Up to 1.2.x all of these were called "WirelessBattery"; ImportOldData() copies that data once.
// Window class and mutex names keep the old prefix on purpose: an old copy and this one must never run
// side by side, and `--quit` of either version closes the other.
// Macros (not constants) so they join other string literals: APP_KEY L"\\Updates".
#define APP_FOLDER L"KJ-BatteryHub"
#define APP_KEY L"Software\\KJ-BatteryHub"
#define APP_RUN_VALUE L"KJ-BatteryHub"
#define APP_EXE L"KJ-BatteryHub.exe"

namespace app_identity {
// Copies the 1.2.x data (settings, battery history, translations, startup entry of this copy) to the new
// names, the first time this version starts. The old data stays where it was, as a backup.
void ImportOldData();
}  // namespace app_identity
