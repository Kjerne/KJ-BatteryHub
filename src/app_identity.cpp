#include "app_identity.h"

#include <windows.h>

#include <string>

namespace app_identity {
namespace {
constexpr const wchar_t* kOldFolder = L"WirelessBattery";
constexpr const wchar_t* kOldKey = L"Software\\WirelessBattery";
constexpr const wchar_t* kOldRunValue = L"WirelessBattery";
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kImportedValue = L"ImportedFromWirelessBattery";

std::wstring Env(const wchar_t* name) {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(buf) : std::wstring();
}

bool IsFolder(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// Copies files matching `pattern` from one folder to another; files already in `to` are kept.
void CopyFiles(const std::wstring& from, const std::wstring& to, const wchar_t* pattern) {
    if (!IsFolder(from)) return;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((from + L"\\" + pattern).c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    CreateDirectoryW(to.c_str(), nullptr);
    do {
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (fd.cFileName[0] == L'_') continue;  // the translation template is rewritten when needed
        CopyFileW((from + L"\\" + fd.cFileName).c_str(), (to + L"\\" + fd.cFileName).c_str(), TRUE);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

std::wstring Folder(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

// An old "Start with Windows" entry that starts a copy in this exe's folder now starts this exe. Entries
// for copies elsewhere are left alone.
void MoveStartupEntry() {
    wchar_t self[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) return;
    wchar_t value[MAX_PATH + 8] = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kOldRunValue, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return;
    std::wstring old = value;
    if (old.size() >= 2 && old.front() == L'"') old = old.substr(1, old.find(L'"', 1) - 1);
    if (_wcsicmp(Folder(old).c_str(), Folder(self).c_str()) != 0) return;
    HKEY run;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &run) != ERROR_SUCCESS) return;
    const std::wstring now = L"\"" + std::wstring(self) + L"\"";
    if (RegSetValueExW(run, APP_RUN_VALUE, 0, REG_SZ, reinterpret_cast<const BYTE*>(now.c_str()),
                       static_cast<DWORD>((now.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS)
        RegDeleteValueW(run, kOldRunValue);
    RegCloseKey(run);
}
}  // namespace

void ImportOldData() {
    // Done once, marked by a value (not by the key: Setup may already have written a setting there).
    DWORD done = 0, size = sizeof(done);
    if (RegGetValueW(HKEY_CURRENT_USER, APP_KEY, kImportedValue, RRF_RT_REG_DWORD, nullptr, &done, &size) == ERROR_SUCCESS && done)
        return;
    // Files first: the value below marks the import as done.
    const std::wstring local = Env(L"LOCALAPPDATA"), roaming = Env(L"APPDATA");
    if (!local.empty())
        CopyFiles(local + L"\\" + kOldFolder, local + L"\\" APP_FOLDER, L"battery_history.tsv");
    if (!roaming.empty()) {
        CreateDirectoryW((roaming + L"\\" APP_FOLDER).c_str(), nullptr);
        CopyFiles(roaming + L"\\" + kOldFolder + L"\\lang", roaming + L"\\" APP_FOLDER L"\\lang", L"*.txt");
    }
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    HKEY old;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kOldKey, 0, KEY_READ, &old) == ERROR_SUCCESS) {
        RegCopyTreeW(old, nullptr, key);  // the 1.2.x settings win over anything Setup wrote
        RegCloseKey(old);
    }
    done = 1;
    RegSetValueExW(key, kImportedValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&done), sizeof(done));
    RegCloseKey(key);
    MoveStartupEntry();
}

}  // namespace app_identity
