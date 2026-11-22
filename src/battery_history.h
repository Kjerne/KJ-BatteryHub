#pragma once
#include <windows.h>
#include <set>
#include <string>
#include <vector>

#include "history_model.h"
#include "runtime_estimate.h"

namespace battery_history {
// Called once before polling starts. Demo data is ephemeral and never written to disk.
void Initialize(bool demo);
// How long readings are kept: 30, 90 (default), 365 days, or 0 = all time. Call before Initialize() to
// apply it while loading; called later, it deletes readings older than the new period at once.
void SetKeepDays(int days);
// How many readings a keep period of `days` would delete now (0 for all time or in --demo).
size_t ExpiredWith(int days);
// Keys that had readings in the history file when Initialize() ran. Filled once before polling starts and
// never changed afterwards, so the poll thread may read it.
const std::set<std::wstring>& KeysAtStartup();
// Stores only valid, connected and awake readings (the caller enforces connection state).
void Observe(const std::wstring& key, int percent, bool charging);
// The same conservative time-to-empty the viewer shows, in hours, or -1 when there is none (charging, too
// few or stale readings); RemainingText formats it ("~3 d 4 h") or returns an empty string.
double HoursRemaining(const std::wstring& key);
std::wstring RemainingText(const std::wstring& key);
// A rough time-to-empty for the hover card while the estimate above is not ready yet: the current discharge
// from as little as 1 hour and 1 percentage point of decline. Always shown as approximate; never used for
// notifications. -1 when there is not even that.
double RoughHoursRemaining(const std::wstring& key);
std::wstring FormatHours(double hours);
// The recorded readings of the last `hours`, oldest first, for the small chart in the hover card.
// secondsAgo is negative (0 = now); nothing is interpolated.
struct Point { long long secondsAgo; int percent; bool charging; };
std::vector<Point> Recent(const std::wstring& key, int hours);
// Empty when the conservative runtime forecast is unavailable; the UI should display % instead.
std::wstring EstimatedRuntimeText(const std::wstring& key);
// The devices the viewer offers as buttons ({key, display name}, in the app's order). Cheap to call often.
void SetDevices(const std::vector<std::pair<std::wstring, std::wstring>>& devices);
// Non-modal native Win32 history viewer; device identity remains the original key. custom: open it on
// the custom period (two date pickers), for the panel's "Custom period..." link.
void Show(HWND owner, const std::wstring& key, const std::wstring& displayName, bool custom = false);
// For the panel's History tab (src/panel.cpp): the readings of [since, until] sorted by time (demo
// readings in --demo), the first reading's time (-1 without readings), the 30-day forecast, the
// "History saved: ..." line (empty in --demo) and a short local date.
std::vector<Sample> Readings(const std::wstring& key, int64_t since, int64_t until);
int64_t Earliest(const std::wstring& key);
RuntimeEstimate Forecast(const std::wstring& key);
std::wstring StorageSummary();
std::wstring ShortDate(int64_t unix);
// Asks first; true when the device's readings were removed.
bool ClearDevice(HWND owner, const std::wstring& key);
// Removes the device's readings without asking ("Forget device"); false when they could not be saved.
bool Forget(const std::wstring& key);

// The viewer's "Estimated time to empty" as text ("~2 d 5 h"), or empty when there is none.
std::wstring TimeToEmpty(const std::wstring& key);
} // namespace battery_history
