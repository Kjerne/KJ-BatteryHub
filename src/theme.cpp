#include "theme.h"

namespace theme {
namespace {
bool g_loaded = false;

// The characters in res/fonts (scripts/make-fonts.py keeps the same list).
[[maybe_unused]] bool Covered(wchar_t c) {  // unused while Family() is Segoe UI only (see below)
    return (c >= 0x20 && c < 0x7F) || (c >= 0xA0 && c < 0x180) || (c >= 0x218 && c <= 0x21B) ||
           (c >= 0x400 && c < 0x460) || c == 0x490 || c == 0x491 || (c >= 0x2010 && c < 0x2027) ||
           c == 0x2030 || c == 0x2039 || c == 0x203A || c == 0x20AC || c == 0x2116 || c == 0x2122 ||
           c == 0x2190 || c == 0x2192 || c == 0x2212 || c == 0x2248 || c == 0x2264 || c == 0x2265 ||
           c == L'\n' || c == L'\r' || c == L'\t';
}

void Load(HINSTANCE inst, const wchar_t* name) {
    HRSRC res = FindResourceW(inst, name, RT_RCDATA);
    HGLOBAL data = res ? LoadResource(inst, res) : nullptr;
    void* bytes = data ? LockResource(data) : nullptr;
    if (!bytes) return;
    DWORD count = 0;
    if (AddFontMemResourceEx(bytes, SizeofResource(inst, res), nullptr, &count) && count) g_loaded = true;
}
}  // namespace

void LoadFonts(HINSTANCE inst) {
    if (g_loaded) return;
    Load(inst, L"FONT_REGULAR");
    Load(inst, L"FONT_BOLD");
}

// One typeface everywhere. The panel, card and history window draw text with GDI, which snaps letters to
// whole pixels; Segoe UI is built for that and stays even at every size, while Noto Sans (made for smooth
// renderers like F1DockTimers' Qt) came out with uneven strokes and gaps inside words at small sizes, and
// mixing the two per string or per size looked inconsistent. Segoe UI also covers every UI language.
// Noto Sans stays registered (LoadFonts) for a later DirectWrite renderer.
const wchar_t* Family(const std::wstring&) { return L"Segoe UI"; }

COLORREF Mix(COLORREF a, COLORREF b, float t) {
    auto ch = [t](int x, int y) { return (BYTE)(x + (y - x) * t + 0.5f); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

}  // namespace theme
